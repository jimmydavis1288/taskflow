#pragma once

#include "JobRepository.h"

#include <map>
#include <mutex>

class JobRegistry final : public JobRepository {
public:
    void add(const Job& job);
    void insert(const Job& job) override;
    void update(const Job& job) override;
    std::optional<Job> find(int id) const override;
    std::vector<Job> list() const override;
    JobStats stats() const override;

    std::vector<Job> all() const;

private:
    mutable std::mutex mutex_;
    std::map<int, Job> jobs_;
};
