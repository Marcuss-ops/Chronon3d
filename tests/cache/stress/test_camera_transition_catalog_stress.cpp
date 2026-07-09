// =============================================================================
// test_camera_transition_catalog_stress.cpp — TICKET-lock-free-shared_mutex
//
// Concurrency stress test for CameraTransitionCatalog after the migration
// to std::shared_mutex + lock-skip-on-frozen. 16 threads hammer create()
// and has() concurrently. Two complementary scenarios:
//
//   1. Post-freeze lock-free fast-path (the dominant production case:
//      ShotTimelineResolver::evaluate() runs every frame, the catalog is
//      frozen at bootstrap, every lookup should hit the acquire-load path).
//
//   2. Pre-freeze mixed register + read — verifies that shared_lock
//      readers and unique_lock writers serialise correctly BEFORE freeze,
//      and that readers transition smoothly to the lock-free fast path
//      after the freeze observation.
//
// No UB / deadlock / torn-read allowed.
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera_v1/shot_timeline.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace chronon3d::camera_v1;

namespace {

constexpr int kPostFreezeIterations = 4000;
constexpr int kPreFreezeReadIterations = 1500;
constexpr unsigned kStressThreads = 16;
constexpr int kDeadlineMs = 30'000;

const std::vector<CameraTransitionKind> kAllKinds{
    CameraTransitionKind::Cut,
    CameraTransitionKind::SmoothBlend,
    CameraTransitionKind::Push,
    CameraTransitionKind::WhipPan,
    CameraTransitionKind::FocusHandoff,
};

} // namespace

TEST_CASE("CameraTransitionCatalog - post-freeze lock-free stress"
          * doctest::test_suite("stress")) {
    CameraTransitionCatalog catalog;
    // Pre-populate so the post-freeze fast path is exercised on every
    // kind — empty map would short-circuit the (it != ...) branch early.
    catalog.register_defaults();

    REQUIRE_NOTHROW(catalog.freeze());
    REQUIRE(catalog.is_frozen());

    auto t_start = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(kStressThreads);
    for (int tid = 0; tid < static_cast<int>(kStressThreads); ++tid) {
        threads.emplace_back([&catalog, tid]() {
            for (int i = 0; i < kPostFreezeIterations; ++i) {
                for (auto k : kAllKinds) {
                    auto t = catalog.create(k);
                    REQUIRE(t != nullptr);
                    // Touch the transition to ensure the factory was
                    // really invoked (catches null-factory bits mis-set).
                    REQUIRE(!t->id().empty());
                }
                for (auto k : kAllKinds) {
                    REQUIRE(catalog.has(k));
                }
            }
        });
    }
    for (auto& t : threads) t.join();

    auto t_end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          t_end - t_start)
                          .count();
    REQUIRE(elapsed_ms < kDeadlineMs);

    // Post-freeze register_transition is forbidden — exception must fire
    // deterministically (no UB from racy writes that would otherwise
    // race the lock-free readers).
    CHECK_THROWS_WITH(
        catalog.register_transition(
            CameraTransitionKind::Cut,
            ShotTimelineResolver::default_cut),
        "CameraTransitionCatalog: frozen");
}

TEST_CASE("CameraTransitionCatalog - pre-freeze concurrent register+read"
          * doctest::test_suite("stress")) {
    CameraTransitionCatalog catalog;
    // Seed one entry so readers always see *something* during the pre-
    // freeze window.
    catalog.register_transition(CameraTransitionKind::Cut,
                                ShotTimelineResolver::default_cut);

    std::atomic<int> registers_done{0};

    auto t_start = std::chrono::steady_clock::now();
    std::vector<std::thread> threads;
    threads.reserve(kStressThreads);

    // ── 1 writer thread ─────────────────────────────────────────────
    // Registers the remaining 4 transitions, then freezes. Each
    // register_transition takes the unique_lock; has() readers
    // concurrently take shared_lock and observe partial state until
    // freeze publication.
    threads.emplace_back([&]() {
        catalog.register_transition(
            CameraTransitionKind::SmoothBlend,
            ShotTimelineResolver::default_smooth_blend);
        catalog.register_transition(CameraTransitionKind::Push,
                                    ShotTimelineResolver::default_push);
        catalog.register_transition(
            CameraTransitionKind::WhipPan,
            ShotTimelineResolver::default_whip_pan);
        catalog.register_transition(
            CameraTransitionKind::FocusHandoff,
            ShotTimelineResolver::default_focus_handoff);
        registers_done.store(5, std::memory_order_release);

        // Hammer register_transition a few times (idempotent overwrites)
        // to ensure the writers' gate closes cleanly under churn.
        for (int i = 0; i < 100; ++i) {
            catalog.register_transition(
                CameraTransitionKind::Cut,
                ShotTimelineResolver::default_cut);
        }
        catalog.freeze();
        registers_done.fetch_add(1, std::memory_order_release);
    });

    // ── 15 reader threads ───────────────────────────────────────────
    // Hammer create + has concurrently while the writer is in flight
    // and after freeze. After freeze, all reads transition to the
    // acquire-load fast path (acquire/release ordering guarantees the
    // committed factories_ map is fully visible).
    for (int tid = 1; tid < static_cast<int>(kStressThreads); ++tid) {
        threads.emplace_back([&catalog, tid]() {
            for (int i = 0; i < kPreFreezeReadIterations; ++i) {
                for (auto k : kAllKinds) {
                    // No assertion on presence pre-freeze: contract is
                    // only "if the factory was registered, create()
                    // returns a valid instance". We do NOT require
                    // every kind to be present until freeze completes.
                    auto t = catalog.create(k);
                    if (t) {
                        (void)t->id();
                    }
                }
                for (auto k : kAllKinds) {
                    (void)catalog.has(k);
                }
            }
        });
    }

    for (auto& t : threads) t.join();

    auto t_end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          t_end - t_start)
                          .count();
    REQUIRE(elapsed_ms < kDeadlineMs);

    CHECK(registers_done.load(std::memory_order_acquire) >= 6);
    REQUIRE(catalog.is_frozen());

    // After freeze, all five kinds must be present and resolvable.
    for (auto k : kAllKinds) {
        CHECK(catalog.has(k));
        auto t = catalog.create(k);
        REQUIRE(t != nullptr);
        CHECK(!t->id().empty());
    }
}
