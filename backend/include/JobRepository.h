#pragma once

#include "Job.h"

#include <cstddef>
#include <optional>
#include <vector>

struct JobStats {
    std::size_t total = 0;
    std::size_t pending = 0;
    std::size_t processing = 0;
    std::size_t completed = 0;
    std::size_t failed = 0;
};

class JobRepository {
public:
    virtual ~JobRepository() = default;

    virtual void insert(const Job& job) = 0;
    virtual std::optional<Job> find(int id) const = 0;
    virtual std::vector<Job> list() const = 0;
    virtual void update(const Job& job) = 0;
    virtual JobStats stats() const = 0;
};
