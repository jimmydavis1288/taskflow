#include "WorkerPool.h"

#include "Worker.h"
#include "JobRegistry.h"

#include <stdexcept>

WorkerPool::WorkerPool(JobQueue& jobQueue, int maxAttempts)
    : jobQueue_(jobQueue),
      jobRepository_(nullptr),
      maxAttempts_(maxAttempts) {
    if (maxAttempts_ <= 0) {
        throw std::invalid_argument("Maximum attempts must be greater than zero");
    }
}

WorkerPool::WorkerPool(
    JobQueue& jobQueue,
    JobRepository& jobRepository,
    int maxAttempts)
    : jobQueue_(jobQueue),
      jobRepository_(&jobRepository),
      maxAttempts_(maxAttempts) {
    if (maxAttempts_ <= 0) {
        throw std::invalid_argument("Maximum attempts must be greater than zero");
    }
}

WorkerPool::~WorkerPool() {
    stop();
}

void WorkerPool::start(std::size_t numberOfWorkers) {
    if (started_ || stopped_) {
        return;
    }

    started_ = true;
    threads_.reserve(numberOfWorkers);
    for (std::size_t index = 0; index < numberOfWorkers; ++index) {
        const std::size_t workerId = index + 1;
        threads_.emplace_back([this, workerId] {
            if (jobRepository_) {
                Worker worker(
                    workerId, jobQueue_, *jobRepository_, maxAttempts_);
                worker.run();
            } else {
                Worker worker(workerId, jobQueue_, maxAttempts_);
                worker.run();
            }
        });
    }
}

void WorkerPool::stop() {
    if (stopped_) {
        return;
    }

    stopped_ = true;
    jobQueue_.shutdown();
    for (std::thread& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}