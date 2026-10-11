#include "Api.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <charconv>
#include <string>
#include <system_error>

namespace {

using Json = nlohmann::json;

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
    return "UNKNOWN";
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
    return "UNKNOWN";
}

Json jobJson(const Job& job) {
    return {
        {"id", job.getId()},
        {"payload", job.getPayload()},
        {"priority", priorityName(job.getPriority())},
        {"status", statusName(job.getStatus())},
        {"attempts", job.getAttempts()}
    };
}

bool parsePriority(const std::string& value, JobPriority& priority) {
    if (value == "HIGH") {
        priority = JobPriority::HIGH;
    } else if (value == "MEDIUM") {
        priority = JobPriority::MEDIUM;
    } else if (value == "LOW") {
        priority = JobPriority::LOW;
    } else {
        return false;
    }
    return true;
}

void sendJson(httplib::Response& response, int status, const Json& body) {
    response.status = status;
    response.set_content(body.dump(), "application/json");
}

}

void configureApi(
    httplib::Server& server,
    JobQueue& jobQueue,
    JobRepository& jobRepository,
    std::atomic<int>& nextJobId) {
    server.Post("/jobs", [&jobQueue, &jobRepository, &nextJobId](
                             const httplib::Request& request,
                             httplib::Response& response) {
        Json requestBody;
        try {
            requestBody = Json::parse(request.body);
        } catch (const Json::parse_error&) {
            sendJson(response, 400, {{"error", "Request body must be valid JSON"}});
            return;
        }

        if (!requestBody.is_object() ||
            !requestBody.contains("payload") ||
            !requestBody["payload"].is_string() ||
            requestBody["payload"].get<std::string>().empty() ||
            !requestBody.contains("priority") ||
            !requestBody["priority"].is_string()) {
            sendJson(response, 400, {
                {"error", "Expected a non-empty string payload and a priority of HIGH, MEDIUM, or LOW"}
            });
            return;
        }

        const std::string payload = requestBody["payload"].get<std::string>();
        const std::string priorityValue = requestBody["priority"].get<std::string>();
        JobPriority priority;
        if (!parsePriority(priorityValue, priority)) {
            sendJson(response, 400, {
                {"error", "Priority must be HIGH, MEDIUM, or LOW"}
            });
            return;
        }

        const int id = nextJobId.fetch_add(1);
        Job job(id, payload, priority);
        try {
            jobRepository.insert(job);
        } catch (const std::exception&) {
            sendJson(response, 503, {
                {"error", "Job could not be persisted; MongoDB is unavailable"}
            });
            return;
        }
        try {
            jobQueue.enqueue(job);
        } catch (const std::exception&) {
            job.markFailed();
            try {
                jobRepository.update(job);
            } catch (const std::exception&) {
                sendJson(response, 503, {
                    {"error", "Job was persisted but could not be enqueued or marked FAILED"}
                });
                return;
            }
            sendJson(response, 503, {
                {"error", "Job was persisted but could not be enqueued"}
            });
            return;
        }
        sendJson(response, 201, {{"id", id}, {"status", statusName(job.getStatus())}});
    });

    server.Get("/jobs", [&jobRepository](
                            const httplib::Request&,
                            httplib::Response& response) {
        Json jobs = Json::array();
        try {
            for (const Job& job : jobRepository.list()) {
                jobs.push_back(jobJson(job));
            }
        } catch (const std::exception&) {
            sendJson(response, 503, {
                {"error", "Could not retrieve jobs from MongoDB"}
            });
            return;
        }
        sendJson(response, 200, jobs);
    });

    server.Get(R"(/jobs/([^/]+))", [&jobRepository](
                                         const httplib::Request& request,
                                         httplib::Response& response) {
        int id = 0;
        const std::string& idText = request.matches[1];
        const auto parsed = std::from_chars(
            idText.data(), idText.data() + idText.size(), id);
        if (parsed.ec != std::errc{} || id <= 0 ||
            parsed.ptr != idText.data() + idText.size()) {
            sendJson(response, 400, {{"error", "Job ID must be a positive integer"}});
            return;
        }

        try {
            const std::optional<Job> job = jobRepository.find(id);
            if (!job) {
                sendJson(response, 404, {{"error", "Job not found"}});
                return;
            }
            sendJson(response, 200, jobJson(*job));
        } catch (const std::exception&) {
            sendJson(response, 503, {
                {"error", "Could not retrieve job from MongoDB"}
            });
            return;
        }
    });

    server.Get("/stats", [&jobRepository](
                             const httplib::Request&,
                             httplib::Response& response) {
        try {
            const JobStats stats = jobRepository.stats();
            sendJson(response, 200, {
                {"total", stats.total},
                {"pending", stats.pending},
                {"processing", stats.processing},
                {"completed", stats.completed},
                {"failed", stats.failed}
            });
        } catch (const std::exception&) {
            sendJson(response, 503, {
                {"error", "Could not retrieve job statistics from MongoDB"}
            });
        }
    });
}
