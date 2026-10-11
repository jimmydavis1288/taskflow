#include "Api.h"
#include "JobRegistry.h"
#include "WorkerPool.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

class FailingJobRepository final : public JobRepository {
public:
    void insert(const Job&) override {
        throw std::runtime_error("simulated database outage");
    }

    std::optional<Job> find(int) const override {
        throw std::runtime_error("simulated database outage");
    }

    std::vector<Job> list() const override {
        throw std::runtime_error("simulated database outage");
    }

    void update(const Job&) override {
        throw std::runtime_error("simulated database outage");
    }

    JobStats stats() const override {
        throw std::runtime_error("simulated database outage");
    }
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}

int main() {
    try {
        JobQueue priorityQueue;
        priorityQueue.enqueue(Job(1, "low", JobPriority::LOW));
        priorityQueue.enqueue(Job(2, "high-first", JobPriority::HIGH));
        priorityQueue.enqueue(Job(3, "medium", JobPriority::MEDIUM));
        priorityQueue.enqueue(Job(4, "high-second", JobPriority::HIGH));
        require(priorityQueue.dequeue()->getId() == 2,
                "HIGH priority should be dequeued first");
        require(priorityQueue.dequeue()->getId() == 4,
                "Equal-priority jobs should remain FIFO");
        require(priorityQueue.dequeue()->getId() == 3,
                "MEDIUM priority should precede LOW");

        JobQueue retryQueue;
        JobRegistry retryRepository;
        const Job success(10, "success");
        const Job failOnce(11, "FAIL_ONCE");
        const Job failAlways(12, "FAIL_ALWAYS");
        retryRepository.insert(success);
        retryRepository.insert(failOnce);
        retryRepository.insert(failAlways);
        retryQueue.enqueue(success);
        retryQueue.enqueue(failOnce);
        retryQueue.enqueue(failAlways);
        WorkerPool workerPool(retryQueue, retryRepository, 3);
        workerPool.start(1);
        workerPool.stop();
        require(retryRepository.find(10)->getStatus() == JobStatus::COMPLETED &&
                    retryRepository.find(10)->getAttempts() == 1,
                "Successful job should complete on first attempt");
        require(retryRepository.find(11)->getStatus() == JobStatus::COMPLETED &&
                    retryRepository.find(11)->getAttempts() == 2,
                "FAIL_ONCE should complete after a retry");
        require(retryRepository.find(12)->getStatus() == JobStatus::FAILED &&
                    retryRepository.find(12)->getAttempts() == 3,
                "FAIL_ALWAYS should become terminal after the attempt limit");

        JobQueue queue;
        JobRegistry repository;
        std::atomic<int> nextJobId{1};
        httplib::Server server;
        configureApi(server, queue, repository, nextJobId);

        const int port = server.bind_to_any_port("127.0.0.1");
        require(port > 0, "Could not bind test HTTP server");

        std::thread serverThread([&server] {
            server.listen_after_bind();
        });
        for (int attempt = 0; attempt < 100 && !server.is_running(); ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(server.is_running(), "Test HTTP server did not start");

        httplib::Client client("127.0.0.1", port);
        const auto created = client.Post(
            "/jobs",
            R"({"payload":"test report","priority":"HIGH"})",
            "application/json");
        require(created && created->status == 201, "POST /jobs should return 201");
        const auto createdBody = nlohmann::json::parse(created->body);
        require(createdBody["id"] == 1, "POST /jobs should return assigned ID");
        require(createdBody["status"] == "PENDING", "New job should be PENDING");

        const auto allJobs = client.Get("/jobs");
        require(allJobs && allJobs->status == 200, "GET /jobs should return 200");
        const auto jobsBody = nlohmann::json::parse(allJobs->body);
        require(jobsBody.size() == 1, "GET /jobs should list the new job");
        require(jobsBody[0]["payload"] == "test report", "Job payload mismatch");

        const auto oneJob = client.Get("/jobs/1");
        require(oneJob && oneJob->status == 200, "GET /jobs/1 should return 200");
        require(nlohmann::json::parse(oneJob->body)["priority"] == "HIGH",
                "GET /jobs/1 should include priority");

        const auto stats = client.Get("/stats");
        require(stats && stats->status == 200, "GET /stats should return 200");
        require(nlohmann::json::parse(stats->body)["pending"] == 1,
                "GET /stats should count pending jobs");

        Job processing = *repository.find(1);
        processing.incrementAttempts();
        processing.markProcessing();
        repository.update(processing);
        const auto processingStats = client.Get("/stats");
        require(processingStats &&
                    nlohmann::json::parse(processingStats->body)["processing"] == 1,
                "GET /stats should reflect processing updates");
        processing.markCompleted();
        repository.update(processing);
        const auto completedJob = client.Get("/jobs/1");
        require(completedJob &&
                    nlohmann::json::parse(completedJob->body)["status"] ==
                        "COMPLETED",
                "GET /jobs/{id} should reflect completion updates");

        const auto invalidPriority = client.Post(
            "/jobs",
            R"({"payload":"invalid","priority":"URGENT"})",
            "application/json");
        require(invalidPriority && invalidPriority->status == 400,
                "Unsupported priority should return 400");

        const auto invalidId = client.Get("/jobs/not-an-id");
        require(invalidId && invalidId->status == 400,
                "Invalid job ID should return 400");

        const auto missingJob = client.Get("/jobs/999");
        require(missingJob && missingJob->status == 404,
                "Unknown job ID should return 404");

        queue.shutdown();
        const auto enqueueFailure = client.Post(
            "/jobs",
            R"({"payload":"queue is shut down","priority":"LOW"})",
            "application/json");
        require(enqueueFailure && enqueueFailure->status == 503,
                "A job that cannot be enqueued should not return success");
        require(repository.find(2)->getStatus() == JobStatus::FAILED,
                "A persisted job that cannot be enqueued should be marked FAILED");
        const auto terminalStats = client.Get("/stats");
        require(terminalStats && terminalStats->status == 200,
                "GET /stats should remain available after queue shutdown");
        const auto terminalStatsBody =
            nlohmann::json::parse(terminalStats->body);
        require(terminalStatsBody["completed"] == 1 &&
                    terminalStatsBody["failed"] == 1,
                "GET /stats should count completed and failed jobs");

        server.stop();
        serverThread.join();

        JobQueue failingQueue;
        FailingJobRepository failingRepository;
        std::atomic<int> failingNextId{1};
        httplib::Server failingServer;
        configureApi(
            failingServer, failingQueue, failingRepository, failingNextId);
        const int failingPort =
            failingServer.bind_to_any_port("127.0.0.1");
        require(failingPort > 0, "Could not bind database-failure test server");
        std::thread failingServerThread([&failingServer] {
            failingServer.listen_after_bind();
        });
        for (int attempt = 0;
             attempt < 100 && !failingServer.is_running();
             ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(failingServer.is_running(),
                "Database-failure test server did not start");

        httplib::Client failingClient("127.0.0.1", failingPort);
        const auto databaseInsertFailure = failingClient.Post(
            "/jobs",
            R"({"payload":"not saved","priority":"HIGH"})",
            "application/json");
        require(databaseInsertFailure && databaseInsertFailure->status == 503,
                "Database insert failure should return 503");
        const auto databaseReadFailure = failingClient.Get("/jobs");
        require(databaseReadFailure && databaseReadFailure->status == 503,
                "Database read failure should return 503");
        const auto databaseStatsFailure = failingClient.Get("/stats");
        require(databaseStatsFailure && databaseStatsFailure->status == 503,
                "Database statistics failure should return 503");
        failingServer.stop();
        failingServerThread.join();

        JobQueue unavailableQueue;
        unavailableQueue.enqueue(Job(99, "database outage"));
        FailingJobRepository unavailableRepository;
        WorkerPool unavailableWorkerPool(
            unavailableQueue, unavailableRepository, 3);
        unavailableWorkerPool.start(1);
        unavailableWorkerPool.stop();
        require(unavailableQueue.empty(),
                "Workers should join after a persistence failure on shutdown");

        std::cout << "API route checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "API route checks failed: " << error.what() << '\n';
        return 1;
    }
}
