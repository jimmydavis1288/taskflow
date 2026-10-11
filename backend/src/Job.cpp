#include "Job.h"

#include <utility>

Job::Job(int id, std::string payload, JobPriority priority)
    : id_(id),
      payload_(std::move(payload)),
      priority_(priority),
      status_(JobStatus::PENDING),
      attempts_(0),
      createdAt_(Clock::now()),
      updatedAt_(createdAt_) {}

Job::Job(
    int id,
    std::string payload,
    JobPriority priority,
    JobStatus status,
    int attempts,
    Clock::time_point createdAt,
    Clock::time_point updatedAt)
    : id_(id),
      payload_(std::move(payload)),
      priority_(priority),
      status_(status),
      attempts_(attempts),
      createdAt_(createdAt),
      updatedAt_(updatedAt) {}

int Job::getId() const noexcept {
    return id_;
}

const std::string& Job::getPayload() const noexcept {
    return payload_;
}

JobPriority Job::getPriority() const noexcept {
    return priority_;
}

JobStatus Job::getStatus() const noexcept {
    return status_;
}

int Job::getAttempts() const noexcept {
    return attempts_;
}

const std::chrono::system_clock::time_point& Job::getCreatedAt() const noexcept {
    return createdAt_;
}

const std::chrono::system_clock::time_point& Job::getUpdatedAt() const noexcept {
    return updatedAt_;
}

void Job::markProcessing() {
    status_ = JobStatus::PROCESSING;
    updatedAt_ = Clock::now();
}

void Job::markPending() {
    status_ = JobStatus::PENDING;
    updatedAt_ = Clock::now();
}

void Job::markCompleted() {
    status_ = JobStatus::COMPLETED;
    updatedAt_ = Clock::now();
}

void Job::markFailed() {
    status_ = JobStatus::FAILED;
    updatedAt_ = Clock::now();
}

void Job::incrementAttempts() {
    ++attempts_;
    updatedAt_ = Clock::now();
}