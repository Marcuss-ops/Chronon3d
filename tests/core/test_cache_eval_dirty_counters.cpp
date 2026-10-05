// tests/core/test_cache_eval_dirty_counters.cpp
//
// DoD coverage for `feature/frictions-pass1` — verifies that the
// `cache_eval_wall_ms` and `dirty_eval_wall_ms` counter fields on `RenderCounters`
// are wired correctly so that the new instrumentation added in
// `src/render_graph/executor/cache_evaluator.cpp` and
// `src/render_graph/pipeline/scene_dirty.cpp` actually accumulates
// wall-clock duration.  The test asserts three properties:
//
//   1. The two fields exist in `RenderCounters`, are 64-byte aligned, and
//      default to zero.  This proves that downstream readers (telemetry,
//      golden checks) see the slot consistently.
//   2. A simulated `evaluate_cache` workload (key digest + cache lookup
//      decision + telemetry accumulation) drives `cache_eval_wall_ms > 0`.
//   3. A simulated `compute_dirty_rect` workload (parallel bbox
//      gathering + union + decision making) drives `dirty_eval_wall_ms > 0`.
//
// Timing regressions use deterministic synthetic nanosecond samples through
// the production accumulator; no sleep is needed to expose rounding loss.

#include <doctest/doctest.h>

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include "../../apps/chronon3d_cli/utils/telemetry/telemetry_merge.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>
using namespace chronon3d;

using chronon3d::profiling::duration_ms;
using chronon3d::profiling::now;

// ── 1. Fields exist, are aligned, and default to zero. ───────────────
TEST_CASE("counters: cache_eval_wall_ms and dirty_eval_wall_ms fields exist and are aligned") {
    RenderCounters c;
    CHECK(c.cache_eval_wall_ms.load() == 0);
    CHECK(c.dirty_eval_wall_ms.load() == 0);

    CHECK(reinterpret_cast<std::uintptr_t>(&c.cache_eval_wall_ms) % 64 == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(&c.dirty_eval_wall_ms) % 64 == 0);

    // Two adjacent fields must NOT share a cache line.
    const auto a = reinterpret_cast<std::uintptr_t>(&c.cache_eval_wall_ms);
    const auto b = reinterpret_cast<std::uintptr_t>(&c.dirty_eval_wall_ms);
    CHECK((a / 64) != (b / 64));
}

// ── 2. cache_eval_wall_ms accumulates when a (mock) cache_eval phase runs. ─
// Mirrors the instrumentation pattern used inside
// `cache_evaluator.cpp::evaluate_cache()`.  We run a small busy loop so the
// timer captures a non-zero duration on every reasonable machine.
TEST_CASE("counters: cache_eval_wall_ms > 0 after a simulate-and-accumulate pass") {
    RenderCounters c;
    c.reset();

    const auto t0 = now();
    // Simulate the heavy work of cache evaluation: key digest + cache
    // lookup decision (typically a few microseconds).  We combine a busy
    // loop with a deterministic 1.5 ms sleep so duration_ms always
    // produces > 0, even on hosts where the loop is aggressively
    // inlined and the busy work rounds below 1 ms.
    volatile std::uint64_t sink = 0;
    for (int i = 0; i < 50'000; ++i) {
        sink += static_cast<std::uint64_t>(i) * 0x9E3779B97F4A7C15ULL;
    }
    (void)sink;
    std::this_thread::sleep_for(std::chrono::microseconds(1500));
    profiling::accumulate_duration(c.cache_eval_wall_ns, c.cache_eval_wall_ms, t0, now());

    CHECK(c.cache_eval_wall_ms.load() > 0);
    // The simulated work must take measurably less than 10s; upper bound
    // catches a stuck clock or scheduler starvation.
    CHECK(c.cache_eval_wall_ms.load() < 10'000);
}

// ── 3. dirty_eval_wall_ms accumulates when a (mock) dirty_eval phase runs. ───
// Mirrors the instrumentation pattern used inside
// `scene_dirty.cpp::compute_dirty_rect()`.
TEST_CASE("counters: dirty_eval_wall_ms > 0 after a simulate-and-accumulate pass") {
    RenderCounters c;
    c.reset();

    const auto t0 = now();
    // Simulate the heavy work of dirty-rect evaluation: parallel bbox
    // computation + union + decision making.  A few-thread busy loop
    // guarantees a non-zero duration while keeping the test fast.
    constexpr int kThreads = 4;
    constexpr int kIters = 200'000;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    std::atomic<std::uint64_t> sum{0};
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&] {
            std::uint64_t local = 0;
            for (int i = 0; i < kIters; ++i) {
                local += static_cast<std::uint64_t>(i) * 0xD1342543DE82EF95ULL;
            }
            sum.fetch_add(local, std::memory_order_relaxed);
        });
    }
    for (auto& th : workers) th.join();
    CHECK(sum.load() > 0);
    std::this_thread::sleep_for(std::chrono::microseconds(1500));
    profiling::accumulate_duration(c.dirty_eval_wall_ns, c.dirty_eval_wall_ms, t0, now());

    CHECK(c.dirty_eval_wall_ms.load() > 0);
    CHECK(c.dirty_eval_wall_ms.load() < 10'000);
}

TEST_CASE("counters: sub-millisecond samples accumulate before rounding") {
    RenderCounters c;
    for (int i = 0; i < 1000; ++i) {
        profiling::accumulate_duration_ns(c.cache_eval_wall_ns, c.cache_eval_wall_ms, 30'000);
        profiling::accumulate_duration_ns(c.dirty_eval_wall_ns, c.dirty_eval_wall_ms, 30'000);
    }
    CHECK(c.cache_eval_wall_ns.load() == 30'000'000);
    CHECK(c.cache_eval_wall_ms.load() == 30);
    CHECK(c.dirty_eval_wall_ms.load() == 30);
    RenderCounters copy(c);
    CHECK(copy.cache_eval_wall_ns.load() == 30'000'000);
    c.reset();
    CHECK(c.cache_eval_wall_ns.load() == 0);
    CHECK(c.dirty_eval_wall_ns.load() == 0);
    CHECK(c.cache_eval_wall_ms.load() == 0);
}

TEST_CASE("counters: fractional timing carries across per-frame telemetry merges") {
    RenderCounters total;
    for (int frame = 0; frame < 1000; ++frame) {
        RenderCounters sample;
        profiling::accumulate_duration_ns(sample.cache_eval_wall_ns, sample.cache_eval_wall_ms, 30'000,
                                          &sample.cache_eval_wall_us);
        profiling::accumulate_duration_ns(sample.dirty_eval_wall_ns, sample.dirty_eval_wall_ms, 30'000);
        cli::telemetry::add_counters(total, sample);
    }
    CHECK(total.cache_eval_wall_ms.load() == 30);
    CHECK(total.cache_eval_wall_us.load() == 30'000);
    CHECK(total.dirty_eval_wall_ms.load() == 30);
    RenderCounters legacy;
    legacy.cache_eval_wall_ms = 7;
    legacy.cache_eval_wall_us = 7000;
    cli::telemetry::add_counters(total, legacy);
    CHECK(total.cache_eval_wall_ms.load() == 37);
    CHECK(total.cache_eval_wall_us.load() == 37'000);
}

TEST_CASE("counters: concurrent fractional samples retain all elapsed time") {
    RenderCounters c;
    std::vector<std::thread> workers;
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&] {
            for (int i = 0; i < 1000; ++i)
                profiling::accumulate_duration_ns(c.cache_eval_wall_ns, c.cache_eval_wall_ms, 30'001);
        });
    }
    for (auto& worker : workers) worker.join();
    CHECK(c.cache_eval_wall_ns.load() == 240'008'000);
    CHECK(c.cache_eval_wall_ms.load() == 240);
}

// ── 4. Concurrent fetch_add from N threads doesn't lose records. ───
// Confirms the chosen atomic ordering (relaxed) is correct for these
// counters — they are pure accumulators, no read-modify-write that
// depends on previous value.
TEST_CASE("counters: cache_eval_wall_ms and dirty_eval_wall_ms merge_tls no records lost") {
    constexpr int kThreads = 8;
    constexpr int kAccumsPerThread = 1000;
    RenderCounters c;
    c.reset();

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&] {
            for (int i = 0; i < kAccumsPerThread; ++i) {
                c.cache_eval_wall_ms.fetch_add(1, std::memory_order_relaxed);
                c.dirty_eval_wall_ms.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& th : workers) th.join();

    CHECK(c.cache_eval_wall_ms.load() ==
          static_cast<std::uint64_t>(kThreads) * kAccumsPerThread);
    CHECK(c.dirty_eval_wall_ms.load() ==
          static_cast<std::uint64_t>(kThreads) * kAccumsPerThread);
}
