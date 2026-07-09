// SPDX-License-Identifier: MIT
//
// test_frame_alloc_counter.cpp — TICKET-121 (Phase 1)
//
// Locks the thread_local isolation contract of FrameAllocCounter:
//   - record_alloc() increments the calling thread's counter.
//   - Two threads have independent counters.
//   - reset_thread_local() zeroes the calling thread's counter only.
//   - The test never touches include/chronon3d/ (no-public-API contract).

#include <doctest/doctest.h>

#include "internal/frame_alloc_counter.hpp"

#include <atomic>
#include <limits>
#include <thread>

using chronon3d::internal::FrameAllocCounter;

TEST_CASE("FrameAllocCounter thread_local isolation (TICKET-121 Phase 1)") {
    // Sanity: main thread has its own counter (zero-init on first access).
    FrameAllocCounter::reset_thread_local();
    CHECK(FrameAllocCounter::current_total_bytes_thread_local() == 0u);

    // Sequence of bumps accumulates monotonically.
    FrameAllocCounter::record_alloc(1024u);
    FrameAllocCounter::record_alloc(2048u);
    CHECK(FrameAllocCounter::current_total_bytes_thread_local() == 3072u);

    // Worker thread bumps ITS OWN counter by 1 MiB.  The main thread's
    // counter MUST stay at 3072 throughout — that is the thread_local
    // isolation contract.
    std::atomic<bool> worker_bumped{false};
    std::thread worker([&worker_bumped]() {
        FrameAllocCounter::reset_thread_local();
        CHECK(FrameAllocCounter::current_total_bytes_thread_local() == 0u);
        FrameAllocCounter::record_alloc(1024u * 1024u);
        CHECK(FrameAllocCounter::current_total_bytes_thread_local()
                == 1024u * 1024u);
        worker_bumped.store(true, std::memory_order_release);
    });
    worker.join();
    REQUIRE(worker_bumped.load(std::memory_order_acquire));

    CHECK(FrameAllocCounter::current_total_bytes_thread_local() == 3072u);

    FrameAllocCounter::reset_thread_local();
    CHECK(FrameAllocCounter::current_total_bytes_thread_local() == 0u);
}

TEST_CASE("FrameAllocCounter zero-init on first thread access (TICKET-121)") {
    // A fresh thread MUST observe a zero counter without any prior reset
    // call.  This is the standard `thread_local` zero-init contract for
    // scalar types.
    std::atomic<std::size_t> observed{std::numeric_limits<std::size_t>::max()};
    std::thread worker([&observed]() {
        observed.store(
            FrameAllocCounter::current_total_bytes_thread_local(),
            std::memory_order_release);
    });
    worker.join();
    CHECK(observed.load(std::memory_order_acquire) == 0u);
}
