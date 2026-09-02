#include <doctest/doctest.h>

#include <chronon3d/runtime/async_encoder_sink.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace chronon3d::runtime;
using namespace std::chrono_literals;

TEST_CASE("AsyncEncoderSink: queue is bounded and applies producer backpressure") {
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool first_entered = false;
    bool release_first = false;

    std::mutex drained_mutex;
    std::vector<std::uint64_t> drained;
    std::size_t flush_count = 0;

    AsyncEncoderSink sink(
        [&](FrameExecutionSlot*, std::uint64_t frame_idx, bool is_flush) {
            if (!is_flush && frame_idx == 0) {
                std::unique_lock gate_lock(gate_mutex);
                first_entered = true;
                gate_cv.notify_all();
                gate_cv.wait(gate_lock, [&]() { return release_first; });
            }

            std::lock_guard drained_lock(drained_mutex);
            if (is_flush) {
                ++flush_count;
            } else {
                drained.push_back(frame_idx);
            }
        },
        1);

    CHECK(sink.queue_capacity() == 1);
    CHECK(sink.state() == AsyncEncoderSinkState::Running);

    sink.submit_frame(nullptr, 0);
    {
        std::unique_lock gate_lock(gate_mutex);
        REQUIRE(gate_cv.wait_for(gate_lock, 1s, [&]() { return first_entered; }));
    }

    // Worker is intentionally held in frame 0, so frame 1 occupies the only
    // queue slot and frame 2 must block until capacity becomes available.
    sink.submit_frame(nullptr, 1);
    CHECK(sink.queue_high_watermark() == 1);

    std::atomic<bool> third_started{false};
    std::atomic<bool> third_returned{false};
    std::thread producer([&]() {
        third_started.store(true, std::memory_order_release);
        sink.submit_frame(nullptr, 2);
        third_returned.store(true, std::memory_order_release);
    });

    while (!third_started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(40ms);
    CHECK_FALSE(third_returned.load(std::memory_order_acquire));

    {
        std::lock_guard gate_lock(gate_mutex);
        release_first = true;
    }
    gate_cv.notify_all();
    producer.join();
    CHECK(third_returned.load(std::memory_order_acquire));

    sink.flush_and_join();
    CHECK(sink.state() == AsyncEncoderSinkState::Closed);
    CHECK(sink.queue_high_watermark() <= sink.queue_capacity());

    std::lock_guard drained_lock(drained_mutex);
    REQUIRE(drained.size() == 3);
    CHECK(drained[0] == 0);
    CHECK(drained[1] == 1);
    CHECK(drained[2] == 2);
    CHECK(flush_count == 1);
}

TEST_CASE("AsyncEncoderSink: drain lifecycle rejects late submit and double flush is idempotent") {
    std::atomic<std::size_t> frame_count{0};
    std::atomic<std::size_t> flush_count{0};

    AsyncEncoderSink sink([&](FrameExecutionSlot*, std::uint64_t, bool is_flush) {
        if (is_flush) {
            ++flush_count;
        } else {
            ++frame_count;
        }
    });

    sink.submit_frame(nullptr, 7);
    sink.flush_and_join();

    CHECK(frame_count.load() == 1);
    CHECK(flush_count.load() == 1);
    CHECK(sink.state() == AsyncEncoderSinkState::Closed);
    CHECK_THROWS_AS(sink.submit_frame(nullptr, 8), std::logic_error);

    sink.flush_and_join();
    CHECK(flush_count.load() == 1);
    CHECK(sink.state() == AsyncEncoderSinkState::Closed);
}

TEST_CASE("AsyncEncoderSink: worker failure closes queue and releases blocked producer") {
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool first_entered = false;
    bool release_failure = false;

    AsyncEncoderSink sink(
        [&](FrameExecutionSlot*, std::uint64_t frame_idx, bool is_flush) {
            if (is_flush) {
                return;
            }
            if (frame_idx == 0) {
                {
                    std::unique_lock gate_lock(gate_mutex);
                    first_entered = true;
                    gate_cv.notify_all();
                    gate_cv.wait(gate_lock, [&]() { return release_failure; });
                }
                throw std::runtime_error("synthetic encoder failure");
            }
        },
        1);

    sink.submit_frame(nullptr, 0);
    {
        std::unique_lock gate_lock(gate_mutex);
        REQUIRE(gate_cv.wait_for(gate_lock, 1s, [&]() { return first_entered; }));
    }
    sink.submit_frame(nullptr, 1);

    std::atomic<bool> blocked_submit_started{false};
    std::atomic<bool> blocked_submit_returned{false};
    std::atomic<bool> blocked_submit_saw_failure{false};
    std::thread blocked_producer([&]() {
        blocked_submit_started.store(true, std::memory_order_release);
        try {
            sink.submit_frame(nullptr, 2);
        } catch (const std::runtime_error&) {
            blocked_submit_saw_failure.store(true, std::memory_order_release);
        }
        blocked_submit_returned.store(true, std::memory_order_release);
    });

    while (!blocked_submit_started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(40ms);
    CHECK_FALSE(blocked_submit_returned.load(std::memory_order_acquire));

    {
        std::lock_guard gate_lock(gate_mutex);
        release_failure = true;
    }
    gate_cv.notify_all();
    blocked_producer.join();

    CHECK(blocked_submit_returned.load(std::memory_order_acquire));
    CHECK(blocked_submit_saw_failure.load(std::memory_order_acquire));
    CHECK(sink.state() == AsyncEncoderSinkState::Closed);
    CHECK_THROWS_AS(sink.flush_and_join(), std::runtime_error);
}

TEST_CASE("AsyncEncoderSink: zero capacity is rejected") {
    CHECK_THROWS_AS(AsyncEncoderSink([](FrameExecutionSlot*, std::uint64_t, bool) {}, 0),
                    std::invalid_argument);
}