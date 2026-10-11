#include "Worker.h"

#include "JobRegistry.h"

#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace {

std::mutex logMutex;

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

void logAttempt(
    std::size_t workerId,
    const char* action,
    const Job& job,
    int attempt) {
    std::lock_guard<std::mutex> lock(logMutex);
    std::cout << "[Worker " << workerId << "] " << action << " Job "
              << job.getId() << " attempt " << attempt << " ("
              << priorityName(job.getPriority()) << ")\n";
}

bool shouldFail(const Job& job) {
    return job.getPayload() == "FAIL_ALWAYS" ||
           (job.getPayload() == "FAIL_ONCE" && job.getAttempts() == 1);
}

}

Worker::Worker(std::size_t id, JobQueue& jobQueue, int maxAttempts)
    : id_(id),
      jobQueue_(jobQueue),
      jobRepository_(nullptr),
      maxAttempts_(maxAttempts) {
    if (maxAttempts_ <= 0) {
        throw std::invalid_argument("Maximum attempts must be greater than zero");
    }
}

Worker::Worker(
    std::size_t id,
    JobQueue& jobQueue,
    JobRepository& jobRepository,
    int maxAttempts)
    : id_(id),
      jobQueue_(jobQueue),
      jobRepository_(&jobRepository),
      maxAttempts_(maxAttempts) {
    if (maxAttempts_ <= 0) {
        throw std::invalid_argument("Maximum attempts must be greater than zero");
    }
}

void Worker::run() {
    JobRegistry legacyRegistry;
    JobRepository& repository =
        jobRepository_ ? *jobRepository_ : static_cast<JobRepository&>(legacyRegistry);

    while (auto queuedJob = jobQueue_.waitDequeue()) {
        Job job = std::move(*queuedJob);
        try {
            if (!repository.find(job.getId())) {
                repository.insert(job);
            }

            Job attempt = job;
            attempt.incrementAttempts();
            attempt.markProcessing();
            repository.update(attempt);
            job = std::move(attempt);
            logAttempt(id_, "Processing", job, job.getAttempts());

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            if (!shouldFail(job)) {
                job.markCompleted();
                repository.update(job);
                logAttempt(id_, "Completed", job, job.getAttempts());
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(logMutex);
                std::cout << "[Worker " << id_ << "] Failed Job " << job.getId()
                          << " on attempt " << job.getAttempts() << '\n';
            }

            if (job.getAttempts() >= maxAttempts_) {
                job.markFailed();
                repository.update(job);
                logAttempt(id_, "Terminal failure", job, job.getAttempts());
                continue;
            }

            const int backoffExponent = job.getAttempts() - 1 < 4
                                            ? job.getAttempts() - 1
                                            : 4;
            const auto backoff =
                std::chrono::milliseconds(25 * (1 << backoffExponent));
            job.markPending();
            repository.update(job);
            {
                std::lock_guard<std::mutex> lock(logMutex);
                std::cout << "[Worker " << id_ << "] Retrying Job "
                          << job.getId() << " after " << backoff.count()
                          << "ms\n";
            }
            std::this_thread::sleep_for(backoff);
            jobQueue_.requeue(job);
        } catch (const std::exception& error) {
            {
                std::lock_guard<std::mutex> lock(logMutex);
                std::cerr << "[Worker " << id_ << "] MongoDB or job-state "
                          << "operation failed for Job " << job.getId()
                          << ": " << error.what() << "; requeueing\n";
            }
            job.markPending();
            try {
                repository.update(job);
            } catch (const std::exception& updateError) {
                std::lock_guard<std::mutex> lock(logMutex);
                std::cerr << "[Worker " << id_
                          << "] Could not persist requeued state for Job "
                          << job.getId() << ": " << updateError.what() << '\n';
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (jobQueue_.isShutdown()) {
                std::lock_guard<std::mutex> lock(logMutex);
                std::cerr << "[Worker " << id_ << "] Shutdown is in progress; "
                          << "Job " << job.getId()
                          << " remains persisted at its last successful state\n";
                continue;
            }
            try {
                jobQueue_.requeue(job);
            } catch (const std::exception& queueError) {
                std::lock_guard<std::mutex> lock(logMutex);
                std::cerr << "[Worker " << id_
                          << "] Could not requeue Job " << job.getId()
                          << ": " << queueError.what() << '\n';
            }
        }
    }
}