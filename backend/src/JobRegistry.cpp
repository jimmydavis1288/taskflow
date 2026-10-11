#include "JobRegistry.h"

#include <stdexcept>
#include <utility>

void JobRegistry::add(const Job& job) {
    insert(job);
}

void JobRegistry::insert(const Job& job) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto result = jobs_.emplace(job.getId(), job);
    if (!result.second) {
        throw std::logic_error("A job with this ID is already registered");
    }
}

void JobRegistry::update(const Job& job) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(job.getId());
    if (found == jobs_.end()) {
        throw std::logic_error("Cannot update an unregistered job");
    }
    found->second = job;
}

std::optional<Job> JobRegistry::find(int id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(id);
    if (found == jobs_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::vector<Job> JobRegistry::all() const {
    return list();
}

std::vector<Job> JobRegistry::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Job> result;
    result.reserve(jobs_.size());
    for (const auto& entry : jobs_) {
        result.push_back(entry.second);
    }
    return result;
}

JobStats JobRegistry::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    JobStats result;
    result.total = jobs_.size();
    for (const auto& entry : jobs_) {
        switch (entry.second.getStatus()) {
        case JobStatus::PENDING:
            ++result.pending;
            break;
        case JobStatus::PROCESSING:
            ++result.processing;
            break;
        case JobStatus::COMPLETED:
            ++result.completed;
            break;
        case JobStatus::FAILED:
            ++result.failed;
            break;
        }
    }
    return result;
}
