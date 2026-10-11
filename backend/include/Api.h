#pragma once

#include "JobQueue.h"
#include "JobRepository.h"

#include <atomic>

namespace httplib {
class Server;
}

void configureApi(
    httplib::Server& server,
    JobQueue& jobQueue,
    JobRepository& jobRepository,
    std::atomic<int>& nextJobId);
