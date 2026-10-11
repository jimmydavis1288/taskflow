#include "JobQueue.h"

#include <stdexcept>

bool JobQueue::JobComparator::operator()(
    const QueuedJob& left,
    const QueuedJob& right) const {
    if (left.job.getPriority() != right.job.getPriority()) {
        return left.job.getPriority() > right.job.getPriority();
    }
    return left.sequence > right.sequence;
}

void JobQueue::enqueue(const Job& job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (isShutdown_) {
            throw std::logic_error("Cannot enqueue into a shut down JobQueue");
        }
        jobs_.push(QueuedJob{job, nextSequence_++});
    }
    conditionVariable_.notify_one();
}

void JobQueue::requeue(const Job& job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (job.getStatus() != JobStatus::PENDING) {
            throw std::logic_error("Only pending jobs can be requeued");
        }
        jobs_.push(QueuedJob{job, nextSequence_++});
    }
    conditionVariable_.notify_one();
}

std::optional<Job> JobQueue::dequeue() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (jobs_.empty()) {
        return std::nullopt;
    }

    std::optional<Job> job(jobs_.top().job);
    jobs_.pop();
    return job;
}

std::optional<Job> JobQueue::waitDequeue() {
    std::unique_lock<std::mutex> lock(mutex_);
    conditionVariable_.wait(lock, [this] {
        return isShutdown_ || !jobs_.empty();
    });

    if (jobs_.empty()) {
        return std::nullopt;
    }

    std::optional<Job> job(jobs_.top().job);
    jobs_.pop();
    return job;
}

void JobQueue::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        isShutdown_ = true;
    }
    conditionVariable_.notify_all();
}

bool JobQueue::isShutdown() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return isShutdown_;
}

void JobQueue::recordCompleted(const Job& job) {
    std::lock_guard<std::mutex> lock(mutex_);
    completedJobs_.push_back(job);
}

void JobQueue::recordTerminalFailure(const Job& job) {
    std::lock_guard<std::mutex> lock(mutex_);
    failedJobs_.push_back(job);
}

bool JobQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.empty();
}

std::size_t JobQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.size();
}

std::vector<Job> JobQueue::completedJobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return completedJobs_;
}

std::vector<Job> JobQueue::failedJobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failedJobs_;
}