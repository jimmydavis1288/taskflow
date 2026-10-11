#include "Api.h"
#include "Job.h"
#include "JobQueue.h"
#include "MongoJobRepository.h"
#include "WorkerPool.h"

#include <httplib.h>
#include <mongocxx/instance.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t shutdownRequested = 0;

void handleShutdownSignal(int) {
    shutdownRequested = 1;
}

}

int main() {
    try {
        mongocxx::instance mongoInstance{};
        const char* connectionUri = std::getenv("TASKFLOW_MONGODB_URI");
        if (!connectionUri || std::string(connectionUri).empty()) {
            throw std::runtime_error(
                "TASKFLOW_MONGODB_URI must contain a MongoDB connection URI");
        }

        MongoJobRepository jobRepository(connectionUri);
        const std::vector<Job> persistedJobs = jobRepository.list();
        int highestId = 0;
        for (const Job& job : persistedJobs) {
            if (job.getId() > highestId) {
                highestId = job.getId();
            }
        }
        if (highestId == std::numeric_limits<int>::max()) {
            throw std::runtime_error("No further integer job IDs are available");
        }
        std::atomic<int> nextJobId{highestId + 1};

        std::cout << "Loaded " << persistedJobs.size()
                  << " persisted job(s). Existing PENDING and PROCESSING jobs "
                     "are not automatically re-enqueued."
                  << std::endl;

        JobQueue jobQueue;
        WorkerPool workerPool(jobQueue, jobRepository);
        httplib::Server server;
        configureApi(server, jobQueue, jobRepository, nextJobId);

        constexpr int port = 8080;
        if (!server.bind_to_port("127.0.0.1", port)) {
            std::cerr << "Failed to bind HTTP server to 127.0.0.1:" << port
                      << '\n';
            return 1;
        }

        std::signal(SIGINT, handleShutdownSignal);
        std::signal(SIGTERM, handleShutdownSignal);
        workerPool.start(3);

        std::atomic<bool> serverExited{false};
        std::thread serverThread([&] {
            if (!server.listen_after_bind()) {
                std::cerr << "HTTP server stopped unexpectedly\n";
            }
            serverExited.store(true);
        });

        std::cout << "TaskFlow API listening at http://127.0.0.1:" << port
                  << std::endl;
        while (shutdownRequested == 0 && !serverExited.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        server.stop();
        if (serverThread.joinable()) {
            serverThread.join();
        }
        workerPool.stop();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TaskFlow startup failed: " << error.what() << '\n';
        return 1;
    }
}