// tests/cli/test_nvml_sampler_lifecycle.cpp
//
// TICKET-DAEMON-JOB-LIFECYCLE — the per-job sampler must not charge its
// sampling interval to the job's teardown.
//
// The sampler runs inside `video_export_pipe` as a per-job local, on the
// SERIALIZED daemon path: `stop()` joins a worker, so everything that worker is
// doing sits inside this job's service time. The first implementation slept a
// full interval and only then checked the stop flag, so a job paid up to one
// whole interval of pure latency — and, because NVML rate-limits the utilization
// queries, sometimes the driver call in flight. Neither was visible to any
// timer, which is how a ~1 s constant hid in the job lifecycle for days.
//
// These cases pin the two halves of the fix:
//   1. the interval wait is INTERRUPTIBLE (pure, so no driver and no timing
//      flakiness is involved: a 5 s interval must return in milliseconds);
//   2. the residual cost of a stop is MEASURED (`NvmlStats::sampler_stop_ms`),
//      so a driver call can be seen instead of inferred.
#include <doctest/doctest.h>

#include "utils/telemetry/nvml_sampler.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace telemetry = chronon3d::cli::telemetry;

namespace {

using clock_t_ = std::chrono::steady_clock;

double ms_since(const clock_t_::time_point& t0) {
    return std::chrono::duration<double, std::milli>(clock_t_::now() - t0).count();
}

} // namespace

TEST_CASE("nvml sampler: the tick wait is interrupted by a stop, never timed out") {
    std::condition_variable wake;
    std::mutex wake_mutex;
    std::atomic<bool> running{true};

    // An interval far longer than the assertion bound: if the wait were a sleep
    // joined by the stopper, this case would take ~5 s and fail.
    const auto interval = std::chrono::milliseconds(5000);
    bool returned_control = true;
    std::thread waiter([&] {
        returned_control = telemetry::wait_for_next_tick(
            wake, wake_mutex, running, interval);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto stop_t0 = clock_t_::now();
    running.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(wake_mutex);
    }
    wake.notify_all();
    waiter.join();
    const double wake_ms = ms_since(stop_t0);

    CHECK_FALSE(returned_control);  // woken by the stop, not by the interval
    CHECK(wake_ms < 250.0);         // milliseconds, not the remaining ~4.95 s
}

TEST_CASE("nvml sampler: an elapsed interval returns control to the loop") {
    std::condition_variable wake;
    std::mutex wake_mutex;
    std::atomic<bool> running{true};

    const auto t0 = clock_t_::now();
    const bool elapsed = telemetry::wait_for_next_tick(
        wake, wake_mutex, running, std::chrono::milliseconds(20));

    CHECK(elapsed);
    const double wait_ms = ms_since(t0);
    CHECK(wait_ms >= 15.0);   // it really waited
    CHECK(wait_ms < 2000.0);  // and it is not a hang
}

TEST_CASE("nvml sampler: a stop is always measured, and a never-started one is free") {
    telemetry::NvmlSampler sampler;

    // Contract for a host (or build) with no NVML: the sampler is inert, so a
    // start/stop pair must cost nothing and must NOT fabricate a measurement.
    if (!sampler.is_available()) {
        sampler.start(std::chrono::milliseconds(250));
        const auto t0 = clock_t_::now();
        sampler.stop();
        CHECK(ms_since(t0) < 100.0);
        CHECK(sampler.stats().sampler_stop_ms == doctest::Approx(-1.0));
        return;
    }

    // With a driver present: the stop cost is reported as a measured value, so
    // an in-flight NVML call can be attributed to the job instead of hiding.
    sampler.start(std::chrono::milliseconds(250));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sampler.stop();
    const auto stats = sampler.stats();
    CHECK(stats.sampler_stop_ms >= 0.0);

    // A second stop performs no work and therefore reports no new measurement:
    // "not stopped by this call" stays distinguishable from "stopped in 0 ms".
    const double first = stats.sampler_stop_ms;
    sampler.stop();
    CHECK(sampler.stats().sampler_stop_ms == doctest::Approx(first));
}
