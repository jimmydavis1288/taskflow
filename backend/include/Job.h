#pragma once

#include <chrono>
#include <string>

enum class JobStatus {
    PENDING,
    PROCESSING,
    COMPLETED,
    FAILED
};

enum class JobPriority {
    HIGH,
    MEDIUM,
    LOW
};

class Job {
public:
    Job(int id, std::string payload, JobPriority priority = JobPriority::MEDIUM);
    Job(
        int id,
        std::string payload,
        JobPriority priority,
        JobStatus status,
        int attempts,
        std::chrono::system_clock::time_point createdAt,
        std::chrono::system_clock::time_point updatedAt);

    int getId() const noexcept;
    const std::string& getPayload() const noexcept;
    JobPriority getPriority() const noexcept;
    JobStatus getStatus() const noexcept;
    int getAttempts() const noexcept;
    const std::chrono::system_clock::time_point& getCreatedAt() const noexcept;
    const std::chrono::system_clock::time_point& getUpdatedAt() const noexcept;

    void markProcessing();
    void markPending();
    void markCompleted();
    void markFailed();
    void incrementAttempts();

private:
    using Clock = std::chrono::system_clock;

    int id_;
    std::string payload_;
    JobPriority priority_;
    JobStatus status_;
    int attempts_;
    Clock::time_point createdAt_;
    Clock::time_point updatedAt_;
};