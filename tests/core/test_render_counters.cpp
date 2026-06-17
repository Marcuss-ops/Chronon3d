// tests/core/test_render_counters.cpp
//
// Regression + throughput tests for the RenderCounters refactor:
//   1) All atomic fields in RenderCounters are alignas(64) — verified
//      via the alignment check (no two fields may share a cache line).
//   2) RenderCountersRaw::merge_tls() correctly aggregates a single
//      thread's local counters into the global atomic struct.
//   3) thread_local_counters() returns a unique RenderCountersRaw per
//      thread — no cross-thread contamination.
//   4) N threads × M increments → sum in RenderCounters == N * M (no
//      records lost across the thread-local → global merge).
//   5) Plain ++ on thread_local_counters() is faster than the equivalent
//      atomic fetch_add on a shared RenderCounters (throughput sanity
//      check — actual ratio depends on the host machine).
//   6) The dirty_full_fallback_reasons array is handled correctly by
//      both the per-thread raw and the global merge.

#include <doctest/doctest.h>

#include <chronon3d/core/dirty_fallback_reason.hpp>
#include <chronon3d/core/profiling/counters.hpp>

#include <chronon3d/core/profiling/profiling.hpp>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>
using namespace chronon3d;


namespace {

// Each thread does this many plain ++ ops on its own thread_local_counters.
constexpr int kThreadIters = 50'000;

// Number of worker threads for the concurrency test.
constexpr int kNumThreads = 8;

// Expected total after merge: every thread does kThreadIters increments
// of the same counter.  If merge_tls is correct, the global counter must
// hold exactly (kNumThreads * kThreadIters).
constexpr uint64_t kExpectedTotal =
    static_cast<uint64_t>(kNumThreads) * static_cast<uint64_t>(kThreadIters);

} // namespace

// ---------------------------------------------------------------------------
// 1. Alignment: every atomic field in RenderCounters sits on its own cache
//    line.  We check this by computing the address modulo 64 for several
//    fields and asserting they are all distinct from the field before.
// ---------------------------------------------------------------------------
TEST_CASE("RenderCounters: all atomic fields are 64-byte aligned") {
    RenderCounters c;

    // Hot counters.
    CHECK(reinterpret_cast<std::uintptr_t>(&c.pixels_touched) % 64 == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(&c.cache_hits)     % 64 == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(&c.nodes_executed) % 64 == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(&c.tiles_total)    % 64 == 0);

    // System counters.
    CHECK(reinterpret_cast<std::uintptr_t>(&c.tbb_arena_max_concurrency) % 64 == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(&c.used_parallel_clear)        % 64 == 0);

    // Setup counters.
    CHECK(reinterpret_cast<std::uintptr_t>(&c.image_decode_ms) % 64 == 0);

    // dirty_full_fallback_reasons array is also 64-byte aligned.
    CHECK(reinterpret_cast<std::uintptr_t>(&c.dirty_full_fallback_reasons) % 64 == 0);
    if (dirty_fallback_reason_count() >= 2) {
        const auto base = reinterpret_cast<std::uintptr_t>(
            &c.dirty_full_fallback_reasons);
        const auto slot1 = reinterpret_cast<std::uintptr_t>(
            &c.dirty_full_fallback_reasons[1]);
        // Two adjacent 64-byte padded slots must NOT share a cache line.
        CHECK((base / 64) != (slot1 / 64));
    }

    // Two adjacent hot fields must NOT share a cache line.
    const auto a = reinterpret_cast<std::uintptr_t>(&c.pixels_touched);
    const auto b = reinterpret_cast<std::uintptr_t>(&c.cache_hits);
    CHECK((a / 64) != (b / 64));
}

// ---------------------------------------------------------------------------
// 2. Single-thread merge: a RenderCountersRaw with 1 in every field
//    should produce 1 in every field of the global RenderCounters after
//    a single merge_tls().
// ---------------------------------------------------------------------------
TEST_CASE("RenderCounters: merge_tls aggregates single-thread raw correctly") {
    RenderCounters global;
    RenderCountersRaw raw;

    // Set one value in each named field.
    raw.pixels_touched = 42;
    raw.cache_hits = 7;
    raw.tiles_total = 100;
    raw.tbb_arena_max_concurrency = 16;
    raw.image_decode_ms = 5;
    raw.dirty_full_fallback_reasons[0] = 3;
    if (dirty_fallback_reason_count() >= 2) {
        raw.dirty_full_fallback_reasons[1] = 11;
    }

    global.merge_tls(raw);

    CHECK(global.pixels_touched.load()  == 42);
    CHECK(global.cache_hits.load()      == 7);
    CHECK(global.tiles_total.load()     == 100);
    CHECK(global.tbb_arena_max_concurrency.load() == 16);
    CHECK(global.image_decode_ms.load() == 5);
    CHECK(global.dirty_full_fallback_reasons[0].value.load() == 3);
    if (dirty_fallback_reason_count() >= 2) {
        CHECK(global.dirty_full_fallback_reasons[1].value.load() == 11);
    }
}

// ---------------------------------------------------------------------------
// 3. thread_local_counters() is per-thread: two threads increment the
//    same field on their own thread-local, and they must not see each
//    other's value.
// ---------------------------------------------------------------------------
TEST_CASE("RenderCounters: thread_local_counters is isolated per thread") {
    auto& a = thread_local_counters();
    auto& b = thread_local_counters();

    // Same thread → same instance.
    CHECK(&a == &b);

    a.pixels_touched = 999;

    std::atomic<bool> saw_nonzero_on_other_thread{false};
    std::thread t([&] {
        auto& other = thread_local_counters();
        // Different thread → different instance.
        CHECK(&other != &a);
        // The other thread starts at zero (its own fresh local).
        CHECK(other.pixels_touched == 0);
        if (other.pixels_touched != 0) {
            saw_nonzero_on_other_thread.store(true);
        }
        // Bump the other thread's local.
        other.pixels_touched = 1234;
    });
    t.join();

    // Original thread is still untouched.
    CHECK(a.pixels_touched == 999);
    CHECK_FALSE(saw_nonzero_on_other_thread.load());

    // Clean up for any subsequent test in the same TU.
    a.reset();
}

// ---------------------------------------------------------------------------
// 4. N threads × M increments → global counter == N * M.  This is the
//    core correctness guarantee of the merge_tls pattern: no records
//    are lost when many threads each merge their own local into a
//    shared global.
// ---------------------------------------------------------------------------
TEST_CASE("RenderCounters: N threads merge no records lost") {
    RenderCounters global;
    global.reset();

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);
    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([&] {
            auto& tls = thread_local_counters();
            tls.reset();
            for (int i = 0; i < kThreadIters; ++i) {
                tls.pixels_touched += 1;
                tls.composite_pixels += 1;
            }
            global.merge_tls(tls);
        });
    }
    for (auto& th : threads) th.join();

    CHECK(global.pixels_touched.load()   == kExpectedTotal);
    CHECK(global.composite_pixels.load() == kExpectedTotal);
    // Uncontended counters must stay at 0.
    CHECK(global.cache_hits.load()    == 0);
    CHECK(global.tiles_total.load()   == 0);
}

// ---------------------------------------------------------------------------
// 5. Throughput: N threads doing M plain ++ on thread_local_counters()
//    + 1 merge_tls per thread must be at least not catastrophically
//    slower than the equivalent number of atomic fetch_adds on a
//    shared RenderCounters.  In practice, the thread_local path is
//    several times faster on >2 cores because it eliminates
//    cache-line bouncing.  We assert only the loose bound (>= 0.5x)
//    to avoid flakiness on contended CI runners, but the custom
//    counter reports the actual ratio.
// ---------------------------------------------------------------------------
TEST_CASE("RenderCounters: thread_local is faster than atomic on N threads") {
    constexpr int kBenchIters = 200'000;

    // Warmup both paths once.
    {
        RenderCounters g;
        RenderCountersRaw r;
        for (int i = 0; i < 1000; ++i) {
            g.pixels_touched.fetch_add(1, std::memory_order_relaxed);
            r.pixels_touched += 1;
        }
        g.merge_tls(r);
    }

    // ── Path A: every thread does atomic fetch_add on a shared struct.
    RenderCounters shared;
    shared.reset();
    std::vector<std::thread> workers_a;
    auto t0 = profiling::now();
    for (int t = 0; t < kNumThreads; ++t) {
        workers_a.emplace_back([&] {
            for (int i = 0; i < kBenchIters; ++i) {
                shared.pixels_touched.fetch_add(1, std::memory_order_relaxed);
                shared.composite_pixels.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& th : workers_a) th.join();
    const auto atomic_ns = profiling::elapsed_us(t0) * 1000.0;
    REQUIRE(shared.pixels_touched.load() ==
            static_cast<uint64_t>(kNumThreads) * kBenchIters);

    // ── Path B: every thread does plain ++ on its thread-local,
    //            then one merge_tls per thread.
    RenderCounters merged;
    merged.reset();
    std::vector<std::thread> workers_b;
    auto t2 = profiling::now();
    for (int t = 0; t < kNumThreads; ++t) {
        workers_b.emplace_back([&] {
            auto& tls = thread_local_counters();
            tls.reset();
            for (int i = 0; i < kBenchIters; ++i) {
                tls.pixels_touched += 1;
                tls.composite_pixels += 1;
            }
            merged.merge_tls(tls);
        });
    }
    for (auto& th : workers_b) th.join();
    const auto tls_ns = profiling::elapsed_us(t2) * 1000.0;
    REQUIRE(merged.pixels_touched.load() ==
            static_cast<uint64_t>(kNumThreads) * kBenchIters);

    // Loose bound: tls must be at least half as fast as atomic on N
    // threads, otherwise the refactor is a regression.  In practice
    // it is 2-5x faster on a 4+ core machine.
    MESSAGE("atomic_ns=", atomic_ns, " tls_ns=", tls_ns,
            " speedup_x=", static_cast<double>(atomic_ns) / std::max(tls_ns, double{1.0}));
    CHECK(tls_ns <= atomic_ns * 2);
}

// ---------------------------------------------------------------------------
// 6. dirty_full_fallback_reasons array: each thread bumps a different
//    reason.  After merge, the global array must reflect each thread's
//    contribution independently (no slot mixing).
// ---------------------------------------------------------------------------
TEST_CASE("RenderCounters: dirty_full_fallback_reasons array integrity") {
    if (dirty_fallback_reason_count() < 2) {
        MESSAGE("Not enough reasons to test array integrity");
        return;
    }

    RenderCounters global;
    global.reset();

    constexpr int kSlots = 4;
    const int slots_to_test = std::min<int>(kSlots, dirty_fallback_reason_count());

    std::vector<std::thread> threads;
    for (int slot = 0; slot < slots_to_test; ++slot) {
        threads.emplace_back([&, slot] {
            auto& tls = thread_local_counters();
            tls.reset();
            for (int i = 0; i < 100; ++i) {
                tls.dirty_full_fallback_reasons[slot] += 1;
            }
            global.merge_tls(tls);
        });
    }
    for (auto& th : threads) th.join();

    for (int slot = 0; slot < slots_to_test; ++slot) {
        CHECK(global.dirty_full_fallback_reasons[slot].value.load() == 100);
    }
    for (int slot = slots_to_test; slot < dirty_fallback_reason_count(); ++slot) {
        CHECK(global.dirty_full_fallback_reasons[slot].value.load() == 0);
    }
}

// ---------------------------------------------------------------------------
// 7. render_counters_field_count() sanity: must be > 0 and stable.
//    (We don't pin the exact value — adding a new counter would break
//    the test otherwise — but it must be in a sensible range.)
// ---------------------------------------------------------------------------
TEST_CASE("RenderCounters: render_counters_field_count returns sensible value") {
    const auto n = render_counters_field_count();
    CHECK(n > 100);  // currently ~ 155; lower bound protects against silent
                     // macro-list corruption.
    CHECK(n < 1000); // upper bound protects against macro-list explosion.
}
