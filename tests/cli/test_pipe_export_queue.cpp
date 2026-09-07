// tests/cli/test_pipe_export_queue.cpp
//
// Wave 3 — RenderFrameQueue (SPSC render→writer handoff over a lock-free
// BoundedSpscRing). Acceptance criteria exercised here:
//   * FIFO ordering and bounded capacity (queue-full backpressure)
//   * blocking push unblocks when the consumer drains
//   * cancellation unblocks a blocked producer without per-frame callbacks
//   * close(): consumer drains remaining items, then closed_and_empty()
//   * fail(): producer stops pushing; consumer still drains handed-off items
//   * push preserves caller ownership when it returns false
//
// The data path is single-producer/single-consumer by construction; tests
// model the producer on a separate thread only when the blocking semantics
// are under test.

#include <doctest/doctest.h>

#include "commands/video/common/pipe_export_queue.hpp"

#include <chronon3d/core/cancellation_token.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

using namespace chronon3d::cli;

namespace {

DirectYuvFramePackage package_for(int64_t frame) {
    DirectYuvFramePackage p;
    p.frame_number = Frame{frame};
    p.direct_yuv = nullptr;
    return p;
}

} // namespace

TEST_CASE("RenderFrameQueue: FIFO order and fixed capacity") {
    RenderFrameQueue q;
    CHECK(q.capacity() == 4);

    RenderFramePackage p = package_for(10);
    CHECK(q.push(p));
    p = package_for(11);
    CHECK(q.push(p));
    p = package_for(12);
    CHECK(q.push(p));
    p = package_for(13);
    CHECK(q.push(p));
    CHECK(q.size_approx() == 4);
    CHECK_FALSE(q.closed_and_empty());

    RenderFramePackage out;
    CHECK(q.try_pop(out));
    CHECK(std::get<DirectYuvFramePackage>(out).frame_number.integral() == 10);
    CHECK(q.try_pop(out));
    CHECK(std::get<DirectYuvFramePackage>(out).frame_number.integral() == 11);
    CHECK(q.try_pop(out));
    CHECK(std::get<DirectYuvFramePackage>(out).frame_number.integral() == 12);
    CHECK(q.try_pop(out));
    CHECK(std::get<DirectYuvFramePackage>(out).frame_number.integral() == 13);
    CHECK(q.size_approx() == 0);
    CHECK_FALSE(q.try_pop(out));
}

TEST_CASE("RenderFrameQueue: full queue blocks the producer until drained") {
    RenderFrameQueue q;
    RenderFramePackage p = package_for(1);
    CHECK(q.push(p));
    p = package_for(2);
    CHECK(q.push(p));
    p = package_for(3);
    CHECK(q.push(p));
    p = package_for(4);
    CHECK(q.push(p));
    CHECK(q.size_approx() == 4);

    // 5th push must block (backpressure) until a consumer pops.
    std::atomic<bool> pushed{false};
    std::thread producer([&] {
        RenderFramePackage fifth = package_for(5);
        if (q.push(fifth)) {
            pushed.store(true, std::memory_order_release);
        }
    });

    // Give the producer time to reach the blocking (full) state.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_FALSE(pushed.load(std::memory_order_acquire));

    RenderFramePackage out;
    CHECK(q.try_pop(out));  // drain one slot
    CHECK(std::get<DirectYuvFramePackage>(out).frame_number.integral() == 1);

    producer.join();
    CHECK(pushed.load(std::memory_order_acquire));
    CHECK(q.size_approx() == 4);
}

TEST_CASE("RenderFrameQueue: cancellation unblocks a blocked producer") {
    RenderFrameQueue q;
    for (int i = 0; i < 4; ++i) {
        RenderFramePackage p = package_for(i + 1);
        REQUIRE(q.push(p));
    }

    chronon3d::CancellationToken token;
    std::atomic<bool> returned{false};
    std::thread producer([&] {
        RenderFramePackage fifth = package_for(5);
        const bool ok = q.push(fifth, &token);
        returned.store(ok, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    token.cancel();
    producer.join();
    CHECK_FALSE(returned.load(std::memory_order_acquire));
    CHECK(q.size_approx() == 4);  // caller kept ownership; nothing was enqueued
}

TEST_CASE("RenderFrameQueue: close drains remaining items then reports empty") {
    RenderFrameQueue q;
    for (int i = 0; i < 3; ++i) {
        RenderFramePackage p = package_for(i + 1);
        REQUIRE(q.push(p));
    }
    q.close();

    // Producer stops pushing once closed.
    RenderFramePackage late = package_for(99);
    CHECK_FALSE(q.push(late));

    CHECK_FALSE(q.closed_and_empty());
    RenderFramePackage out;
    CHECK(q.try_pop(out));
    CHECK(q.try_pop(out));
    CHECK(q.try_pop(out));
    CHECK(q.closed_and_empty());
    CHECK_FALSE(q.try_pop(out));
}

TEST_CASE("RenderFrameQueue: fail stops producers but consumers still drain") {
    RenderFrameQueue q;
    RenderFramePackage p = package_for(1);
    CHECK(q.push(p));
    p = package_for(2);
    CHECK(q.push(p));
    q.fail();

    RenderFramePackage late = package_for(3);
    CHECK_FALSE(q.push(late));
    CHECK_FALSE(q.closed_and_empty());  // two items still handed off

    RenderFramePackage out;
    CHECK(q.try_pop(out));
    CHECK(std::get<DirectYuvFramePackage>(out).frame_number.integral() == 1);
    CHECK(q.try_pop(out));
    CHECK(std::get<DirectYuvFramePackage>(out).frame_number.integral() == 2);
    CHECK(q.closed_and_empty());
}

TEST_CASE("RenderFrameQueue: pop_for returns enqueued items before the deadline") {
    RenderFrameQueue q;
    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        RenderFramePackage p = package_for(7);
        (void)q.push(p);
    });
    RenderFramePackage out;
    const bool popped = q.pop_for(out, std::chrono::milliseconds(500));
    producer.join();
    CHECK(popped);
    CHECK(std::get<DirectYuvFramePackage>(out).frame_number.integral() == 7);
}

TEST_CASE("RenderFrameQueue: interleaved producer/consumer stays ordered") {
    RenderFrameQueue q;
    constexpr int kItems = 64;  // many wraps of the 4-slot ring
    std::atomic<bool> producer_done{false};
    std::thread producer([&] {
        for (int i = 0; i < kItems; ++i) {
            RenderFramePackage p = package_for(i);
            // push blocks internally while the ring is full; the consumer
            // below drains concurrently, so the queue is never closed here.
            if (!q.push(p)) break;
        }
        producer_done.store(true, std::memory_order_release);
    });

    RenderFramePackage out;
    int expected = 0;
    while (!producer_done.load(std::memory_order_acquire) || q.size_approx() > 0) {
        if (q.try_pop(out)) {
            CHECK(std::get<DirectYuvFramePackage>(out).frame_number.integral() == expected);
            ++expected;
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }
    producer.join();
    CHECK(expected == kItems);
}
