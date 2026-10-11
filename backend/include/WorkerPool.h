#pragma once

#include "JobQueue.h"
#include "JobRepository.h"

#include <cstddef>
#include <thread>
#include <vector>

class WorkerPool {
public:
    explicit WorkerPool(JobQueue& jobQueue, int maxAttempts = 3);
    explicit WorkerPool(
        JobQueue& jobQueue,
        JobRepository& jobRepository,
        int maxAttempts = 3);
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void start(std::size_t numberOfWorkers);
    void stop();

private:
    JobQueue& jobQueue_;
    JobRepository* jobRepository_;
    int maxAttempts_;
    std::vector<std::thread> threads_;
    bool started_ = false;
    bool stopped_ = false;
};