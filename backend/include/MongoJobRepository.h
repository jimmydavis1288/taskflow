#pragma once

#include "JobRepository.h"

#include <memory>
#include <string>

class MongoJobRepository final : public JobRepository {
public:
    explicit MongoJobRepository(const std::string& connectionUri);
    ~MongoJobRepository() override;

    MongoJobRepository(const MongoJobRepository&) = delete;
    MongoJobRepository& operator=(const MongoJobRepository&) = delete;

    void insert(const Job& job) override;
    std::optional<Job> find(int id) const override;
    std::vector<Job> list() const override;
    void update(const Job& job) override;
    JobStats stats() const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
