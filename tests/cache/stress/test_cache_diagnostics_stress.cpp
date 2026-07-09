// =============================================================================
// test_cache_diagnostics_stress.cpp — TICKET-lock-free-shared_mutex
//
// Concurrency stress test exercising concurrent register/unregister against
// shared_lock reads (snapshot / registered_count) and unique_lock writes
// (clear_all). 16 threads hammer the registry for short-burst cycles; no
// crash / UB / torn-read / deadlock allowed.
//
// Run via the chronon3d_cache_tests ctest target (CTest tag [stress]).
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/cache_policy.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace chronon3d::cache;

namespace {

// 16 threads reproduces a busy production hot path (cache_diagnostics is
// queried by telemetry dashboards, frame-level snapshot dumps, and CLI
// status commands concurrently with frame-graph cache insertions).
constexpr unsigned kStressThreads = 16;
constexpr int kReadIterations = 1500;
constexpr int kChurnIterations = 400;
constexpr int kClearIterations = 80;
constexpr int kDeadlineMs = 30'000;  // CI hang guard

/// Sentinel state attached to each registered cache so concurrent
/// stats_fn() readers and clear_fn() writers observe a non-trivial value
/// (no constant-folded elimination). Held by-reference inside the
/// lambdas — lifetime bounded by the local scope of the registering
/// thread, so the entry's clear_fn references remain valid as long as
/// the entry exists in the registry (Handle drop deletes the entry).
struct StressEntry {
    std::atomic<int> tick{0};
};

} // namespace

TEST_CASE("CacheDiagnostics - shared_mutex concurrent stress"
          * doctest::test_suite("stress")) {
    auto& diag = CacheDiagnostics::instance();
    diag.set_enabled(true);
    // Drain any residual state from prior test cases (regression guard for
    // the cross-test singleton).
    diag.clear_all();

    REQUIRE(diag.registered_count() == 0);

    auto t_start = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(kStressThreads);

    // ── 8 reader threads ─────────────────────────────────────────────
    // Exercise concurrent shared_lock acquisition. Writers (churn + clear)
    // must serialise against them; readers must NOT serialise against each
    // other (the whole point of the migration).
    for (int tid = 0; tid < 8; ++tid) {
        threads.emplace_back([&diag, tid]() {
            for (int i = 0; i < kReadIterations; ++i) {
                (void)diag.snapshot();
                (void)diag.snapshot_all_domains();
                (void)diag.snapshot_by_domain(CacheDomain::Nodes);
                (void)diag.snapshot_by_domain(CacheDomain::RenderedFrames);
                (void)diag.snapshot_by_domain(CacheDomain::ScenePrograms);
                (void)diag.registered_count();
                (void)diag.registered_count(CacheDomain::Text);
            }
        });
    }

    // ── 4 churn threads ─────────────────────────────────────────────
    // Register + auto-drop Handle per iteration. The captured-by-reference
    // StressEntry local is destroyed at iteration end, but the entry
    // itself is only deleted by the Handle destructor inside unregister()
    // (which takes unique_lock — correctly serialised against clear_all()
    // so an in-flight clear_fn() cannot race the entry's destruction).
    for (int tid = 0; tid < 4; ++tid) {
        threads.emplace_back([&diag, tid]() {
            for (int i = 0; i < kChurnIterations; ++i) {
                StressEntry e;
                CacheDomain d =
                    static_cast<CacheDomain>(((tid + 1) * 1009 + i) % 4 + 1);
                auto h = diag.register_cache(
                    d,
                    [&e]() -> GenericCacheStats {
                        return GenericCacheStats{
                            static_cast<std::size_t>(
                                e.tick.load(std::memory_order_relaxed)),
                            0, 0, 1, 0
                        };
                    },
                    [&e] {
                        e.tick.fetch_add(1, std::memory_order_relaxed);
                    },
                    [] { return CapacityMode::Weight; },
                    1024
                );
                REQUIRE(static_cast<bool>(h));
                // Handle drops at end of iteration -> unregister fires.
            }
        });
    }

    // ── 4 clearer threads ────────────────────────────────────────────
    // Exercise unique_lock mutation path (clear_fn callback invocation).
    // No re-entrancy: clear_fn only bumps the per-entry atomic,
    // never registers new entries, so no deadlock is reachable.
    for (int tid = 0; tid < 4; ++tid) {
        threads.emplace_back([&diag]() {
            for (int i = 0; i < kClearIterations; ++i) {
                (void)diag.clear_all();
            }
        });
    }

    for (auto& t : threads) t.join();

    auto t_end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          t_end - t_start)
                          .count();
    REQUIRE(elapsed_ms < kDeadlineMs);  // deadlock / hang guard

    // All RAII Handles dropped -> registry is empty.
    CHECK(diag.registered_count() == 0);

    // Toggle boundary — make sure set_enabled(false) gates reads without
    // crashing (was a regression vector pre-migration when the atomic
    // publish raced the lock_guard).
    diag.set_enabled(false);
    CHECK(diag.snapshot().empty());
    CHECK(diag.snapshot_all_domains().empty());
    CHECK(diag.snapshot_by_domain(CacheDomain::Nodes).instance_count == 0);
    diag.set_enabled(true);
    CHECK(diag.is_enabled());

    // Final drain.
    diag.clear_all();
    CHECK(diag.registered_count() == 0);
}
