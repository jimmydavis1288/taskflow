#include "MongoJobRepository.h"

#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/options/find.hpp>
#include <mongocxx/options/index.hpp>
#include <mongocxx/pipeline.hpp>
#include <mongocxx/pool.hpp>
#include <mongocxx/uri.hpp>

#include <cstdint>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

namespace {

using bsoncxx::builder::basic::make_array;
using bsoncxx::builder::basic::make_document;
using bsoncxx::builder::basic::kvp;

const char* statusName(JobStatus status) {
    switch (status) {
    case JobStatus::PENDING:
        return "PENDING";
    case JobStatus::PROCESSING:
        return "PROCESSING";
    case JobStatus::COMPLETED:
        return "COMPLETED";
    case JobStatus::FAILED:
        return "FAILED";
    }
    throw std::runtime_error("Cannot serialize an unknown job status");
}

const char* priorityName(JobPriority priority) {
    switch (priority) {
    case JobPriority::HIGH:
        return "HIGH";
    case JobPriority::MEDIUM:
        return "MEDIUM";
    case JobPriority::LOW:
        return "LOW";
    }
    throw std::runtime_error("Cannot serialize an unknown job priority");
}

JobStatus parseStatus(const std::string& value) {
    if (value == "PENDING") {
        return JobStatus::PENDING;
    }
    if (value == "PROCESSING") {
        return JobStatus::PROCESSING;
    }
    if (value == "COMPLETED") {
        return JobStatus::COMPLETED;
    }
    if (value == "FAILED") {
        return JobStatus::FAILED;
    }
    throw std::runtime_error("MongoDB job document has an invalid status");
}

JobPriority parsePriority(const std::string& value) {
    if (value == "HIGH") {
        return JobPriority::HIGH;
    }
    if (value == "MEDIUM") {
        return JobPriority::MEDIUM;
    }
    if (value == "LOW") {
        return JobPriority::LOW;
    }
    throw std::runtime_error("MongoDB job document has an invalid priority");
}

std::string readString(
    const bsoncxx::document::view& document,
    const char* fieldName) {
    const auto element = document[fieldName];
    if (!element || element.type() != bsoncxx::type::k_string) {
        throw std::runtime_error(
            std::string("MongoDB job document is missing string field ") +
            fieldName);
    }
    const auto value = element.get_string().value;
    return std::string(value.data(), value.size());
}

std::int32_t readInt32(
    const bsoncxx::document::view& document,
    const char* fieldName) {
    const auto element = document[fieldName];
    if (!element || element.type() != bsoncxx::type::k_int32) {
        throw std::runtime_error(
            std::string("MongoDB job document is missing int32 field ") +
            fieldName);
    }
    return element.get_int32().value;
}

Job deserialize(const bsoncxx::document::view& document) {
    const auto createdAt = document["createdAt"];
    const auto updatedAt = document["updatedAt"];
    if (!createdAt || createdAt.type() != bsoncxx::type::k_date ||
        !updatedAt || updatedAt.type() != bsoncxx::type::k_date) {
        throw std::runtime_error(
            "MongoDB job document has invalid timestamp fields");
    }

    return Job(
        readInt32(document, "id"),
        readString(document, "payload"),
        parsePriority(readString(document, "priority")),
        parseStatus(readString(document, "status")),
        readInt32(document, "attempts"),
        std::chrono::system_clock::time_point{
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                createdAt.get_date().value)},
        std::chrono::system_clock::time_point{
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                updatedAt.get_date().value)});
}

bsoncxx::document::value serialize(const Job& job) {
    return make_document(
        kvp("id", static_cast<std::int32_t>(job.getId())),
        kvp("payload", job.getPayload()),
        kvp("priority", priorityName(job.getPriority())),
        kvp("status", statusName(job.getStatus())),
        kvp("attempts", static_cast<std::int32_t>(job.getAttempts())),
        kvp("createdAt", bsoncxx::types::b_date{job.getCreatedAt()}),
        kvp("updatedAt", bsoncxx::types::b_date{job.getUpdatedAt()}));
}

bsoncxx::document::value statusCount(const char* status) {
    return make_document(kvp(
        "$sum",
        make_document(kvp(
            "$cond",
            make_array(
                make_document(kvp(
                    "$eq", make_array("$status", status))),
                1,
                0)))));
}

std::size_t countValue(const bsoncxx::document::view& document, const char* key) {
    const auto element = document[key];
    if (!element) {
        return 0;
    }
    if (element.type() == bsoncxx::type::k_int32) {
        return static_cast<std::size_t>(element.get_int32().value);
    }
    if (element.type() == bsoncxx::type::k_int64) {
        return static_cast<std::size_t>(element.get_int64().value);
    }
    throw std::runtime_error("MongoDB returned an invalid job statistics count");
}

}

class MongoJobRepository::Impl {
public:
    explicit Impl(const std::string& connectionUri)
        : pool(mongocxx::uri{connectionUri}) {
        auto client = pool.acquire();
        mongocxx::options::index indexOptions;
        indexOptions.unique(true);
        auto collection = (*client)["taskflow"]["jobs"];
        collection.create_index(
            make_document(kvp("id", 1)), indexOptions);

        auto cursor = collection.find(make_document());
        for (const auto& document : cursor) {
            Job job = deserialize(document);
            jobs.emplace(job.getId(), std::move(job));
        }
    }

    mongocxx::pool pool;
    mutable std::mutex jobsMutex;
    std::map<int, Job> jobs;
};

MongoJobRepository::MongoJobRepository(const std::string& connectionUri)
    : impl_(std::make_unique<Impl>(connectionUri)) {}

MongoJobRepository::~MongoJobRepository() = default;

void MongoJobRepository::insert(const Job& job) {
    if (job.getId() <= 0) {
        throw std::invalid_argument("Job ID must be positive");
    }
    if (job.getAttempts() < 0) {
        throw std::invalid_argument("Job attempts cannot be negative");
    }

    auto client = impl_->pool.acquire();
    (*client)["taskflow"]["jobs"].insert_one(serialize(job));
    std::lock_guard<std::mutex> lock(impl_->jobsMutex);
    const auto result = impl_->jobs.emplace(job.getId(), job);
    if (!result.second) {
        throw std::logic_error("A job with this ID is already cached");
    }
}

std::optional<Job> MongoJobRepository::find(int id) const {
    auto client = impl_->pool.acquire();
    const auto document = (*client)["taskflow"]["jobs"].find_one(
        make_document(kvp("id", static_cast<std::int32_t>(id))));
    if (!document) {
        std::lock_guard<std::mutex> lock(impl_->jobsMutex);
        impl_->jobs.erase(id);
        return std::nullopt;
    }

    Job job = deserialize(document->view());
    {
        std::lock_guard<std::mutex> lock(impl_->jobsMutex);
        impl_->jobs.insert_or_assign(id, job);
    }
    return job;
}

std::vector<Job> MongoJobRepository::list() const {
    auto client = impl_->pool.acquire();
    mongocxx::options::find options;
    options.sort(make_document(kvp("id", 1)));
    auto cursor = (*client)["taskflow"]["jobs"].find(make_document(), options);

    std::vector<Job> jobs;
    std::map<int, Job> refreshedCache;
    for (const auto& document : cursor) {
        Job job = deserialize(document);
        jobs.push_back(job);
        refreshedCache.emplace(job.getId(), std::move(job));
    }
    {
        std::lock_guard<std::mutex> lock(impl_->jobsMutex);
        impl_->jobs.swap(refreshedCache);
    }
    return jobs;
}

void MongoJobRepository::update(const Job& job) {
    if (job.getAttempts() < 0) {
        throw std::invalid_argument("Job attempts cannot be negative");
    }

    auto client = impl_->pool.acquire();
    const auto result = (*client)["taskflow"]["jobs"].update_one(
        make_document(kvp("id", static_cast<std::int32_t>(job.getId()))),
        make_document(kvp(
            "$set",
            make_document(
                kvp("status", statusName(job.getStatus())),
                kvp("attempts", static_cast<std::int32_t>(job.getAttempts())),
                kvp("updatedAt", bsoncxx::types::b_date{job.getUpdatedAt()})))));
    if (!result || result->matched_count() != 1) {
        throw std::runtime_error("MongoDB job update matched no document");
    }
    std::lock_guard<std::mutex> lock(impl_->jobsMutex);
    const auto cachedJob = impl_->jobs.find(job.getId());
    if (cachedJob == impl_->jobs.end()) {
        throw std::logic_error("Updated MongoDB job is not present in the cache");
    }
    cachedJob->second = job;
}

JobStats MongoJobRepository::stats() const {
    auto client = impl_->pool.acquire();
    mongocxx::pipeline pipeline;
    pipeline.group(make_document(
        kvp("_id", bsoncxx::types::b_null{}),
        kvp("total", make_document(kvp("$sum", 1))),
        kvp("pending", statusCount("PENDING")),
        kvp("processing", statusCount("PROCESSING")),
        kvp("completed", statusCount("COMPLETED")),
        kvp("failed", statusCount("FAILED"))));

    auto cursor =
        (*client)["taskflow"]["jobs"].aggregate(pipeline);
    for (const auto& document : cursor) {
        const auto view = document;
        return JobStats{
            countValue(view, "total"),
            countValue(view, "pending"),
            countValue(view, "processing"),
            countValue(view, "completed"),
            countValue(view, "failed")};
    }
    return {};
}
