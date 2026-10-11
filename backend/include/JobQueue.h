#pragma once

#include "Job.h"

#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <vector>

class JobQueue {
public:
    void enqueue(const Job& job);
    void requeue(const Job& job);
    std::optional<Job> dequeue();
    std::optional<Job> waitDequeue();
    void shutdown();
    bool isShutdown() const;
    void recordCompleted(const Job& job);
    void recordTerminalFailure(const Job& job);

    bool empty() const;
    std::size_t size() const;
    std::vector<Job> completedJobs() const;
    std::vector<Job> failedJobs() const;

private:
    struct QueuedJob {
        Job job;
        std::uint64_t sequence;
    };

    struct JobComparator {
        bool operator()(const QueuedJob& left, const QueuedJob& right) const;
    };

    std::priority_queue<QueuedJob, std::vector<QueuedJob>, JobComparator> jobs_;
    std::vector<Job> completedJobs_;
    std::vector<Job> failedJobs_;
    mutable std::mutex mutex_;
    std::condition_variable conditionVariable_;
    bool isShutdown_ = false;
    std::uint64_t nextSequence_ = 0;
};