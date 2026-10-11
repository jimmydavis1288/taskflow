#pragma once

#include "JobQueue.h"
#include "JobRepository.h"

#include <cstddef>

class Worker {
public:
    Worker(std::size_t id, JobQueue& jobQueue, int maxAttempts = 3);
    Worker(
        std::size_t id,
        JobQueue& jobQueue,
        JobRepository& jobRepository,
        int maxAttempts = 3);

    void run();

private:
    std::size_t id_;
    JobQueue& jobQueue_;
    JobRepository* jobRepository_;
    int maxAttempts_;
};