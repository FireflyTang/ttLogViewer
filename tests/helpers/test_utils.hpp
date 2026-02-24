#pragma once
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

#include "log_reader.hpp"

// Wait until LogReader finishes background indexing.
// Phase 1: isIndexing() is always false, so this returns immediately.
// Phase 2: polls with a timeout.
inline void waitForIndexing(
    LogReader& reader,
    std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (reader.isIndexing()) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("waitForIndexing: timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// Wait for a reprocess() DoneCallback to fire.
// Usage:
//   std::promise<void> done;
//   chain.reprocess(0,
//       [](double){},
//       [&]{ done.set_value(); });
//   waitForReprocess(done);
inline void waitForReprocess(
    std::promise<void>& done,
    std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
    auto future = done.get_future();
    if (future.wait_for(timeout) == std::future_status::timeout)
        throw std::runtime_error("waitForReprocess: timeout");
}
