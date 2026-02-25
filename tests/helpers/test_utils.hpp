#pragma once
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

#include "i_log_reader.hpp"

// Wait until LogReader finishes background indexing.
// Returns immediately when isIndexing() is false (includes the case where
// indexing is done synchronously or was never started).
inline void waitForIndexing(
    ILogReader& reader,
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
