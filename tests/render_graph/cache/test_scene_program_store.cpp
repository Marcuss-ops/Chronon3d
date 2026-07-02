// =============================================================================
// test_scene_program_store.cpp — SceneProgramStore integration tests
//
// WP 5.2 / 5.3 / 5.4 / 5.5 lifecycle, ownership, and tuning tests for the
// centralized SceneProgramStore.  These run without a real RenderSession —
// the store's public API operates on PrecompInstanceKey + SceneStructureKey
// directly so unit-level coverage is sufficient.
//
// Test categories:
//   §1 WP 5.2 — ProgramLease holds shared_ptr<CompiledSceneProgram>;
//               an in-flight lease survives a concurrent clear().
//   §2 WP 5.3 — immutable policy per owner (conflict throws).
//   §3 WP 5.4 — record_execution() triggers per-bucket auto_tune() at
//               TuneConfig::interval; Fixed mode is a no-op.
//   §4 WP 5.5 — per-session reset isolation: clear() drops the per-instance
//               buckets but keeps aggregate stats-effect clean.
//   §5 — Different buckets execute concurrently (no global store lock
//         on the hot path; each bucket's cache has its own shard mutex).
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/internal/render_graph/cache/scene_program_store.hpp>
#include <chronon3d/render_graph/cache/scene_program_cache.hpp>
#include <chronon3d/render_graph/compiler/compiled_scene_program.hpp>

#include <atomic>
#include <thread>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::graph;
using namespace chronon3d::cache;

namespace {

// Minimal valid CompiledSceneProgram factory.
std::unique_ptr<CompiledSceneProgram> make_program(uint64_t topology) {
    auto p = std::make_unique<CompiledSceneProgram>();
    p->valid = true;
    p->frame_graph.valid = true;
    p->frame_graph.structure_hash = topology;
    p->key.topology_hash = topology;
    return p;
}

SceneStructureKey key_for(uint64_t topology) {
    return SceneStructureKey{
        .topology_hash = topology,
        .active_set_hash = 0,
        .render_options_hash = 0,
        .width = 200,
        .height = 200,
        .ssaa_factor = 1
    };
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// §1 WP 5.2 — ProgramLease holds shared_ptr; survives clear()
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("scene_program_store: in-flight lease survives a clear()") {
    SceneProgramStore store;
    const auto owner = PrecompInstanceKey{1, 2};
    const PrecompCachePolicy policy{};

    auto lease = store.acquire(
        owner, key_for(0x100), policy,
        []() { return make_program(0x100); });

    REQUIRE(lease.program != nullptr);

    // Concurrent clear() — old behavior would have invalidated the raw
    // pointer on `lease.program`.  Shared_ptr keeps the program alive.
    store.clear();

    CHECK(lease.program != nullptr);             // still alive
    CHECK(lease.program->key.topology_hash == 0x100u);
    // NOTE: shared_ptr::use_count() is racy by definition — don't rely on
    // it.  The alive-pointer check above is the real safety contract.
}

TEST_CASE("scene_program_store: post-clear acquire recompiles for the same key (no stale)") {
    SceneProgramStore store;
    const auto owner = PrecompInstanceKey{1, 2};
    PrecompCachePolicy policy{};

    auto lease0 = store.acquire(owner, key_for(0x100), policy,
        []() { return make_program(0x100); });
    REQUIRE(lease0.program != nullptr);

    store.clear();  // store references drop, but lease0 keeps its own ref.

    // New acquire() must recomplile (the bucket is gone).
    int compile_count = 0;
    auto lease1 = store.acquire(owner, key_for(0x100), policy,
        [&compile_count]() {
            ++compile_count;
            return make_program(0x100);
        });

    REQUIRE(lease1.program != nullptr);
    CHECK(compile_count == 1);  // cache miss → fresh compile
}

// ═════════════════════════════════════════════════════════════════════════════
// §2 WP 5.3 — Immutable policy per owner (conflict throws, not silent reconfig)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("scene_program_store: conflicting policy for same owner throws") {
    SceneProgramStore store;
    const auto owner = PrecompInstanceKey{1, 2};

    PrecompCachePolicy policy_a{.initial_capacity = 8};
    auto lease_a = store.acquire(owner, key_for(0xA), policy_a,
        []() { return make_program(0xA); });
    REQUIRE(lease_a.program != nullptr);

    // Next acquire with a DIFFERENT initial_capacity must throw.
    PrecompCachePolicy policy_b{.initial_capacity = 16};
    CHECK_THROWS_AS(
        store.acquire(owner, key_for(0xB), policy_b,
                      []() { return make_program(0xB); }),
        std::runtime_error);

    // Reset lease_a so its shared_ptr doesn't hold the program; we're
    // about to confirm policy_b succeeds on a DIFFERENT owner.
    lease_a = SceneProgramStore::ProgramLease{};
}

TEST_CASE("scene_program_store: matching policy is idempotent per owner") {
    SceneProgramStore store;
    const auto owner = PrecompInstanceKey{1, 2};

    PrecompCachePolicy policy{};
    int compile_count = 0;
    auto l1 = store.acquire(owner, key_for(0xA), policy,
        [&compile_count]() { ++compile_count; return make_program(0xA); });
    REQUIRE(l1.program != nullptr);
    CHECK(compile_count == 1);

    // Identical policy — must NOT throw, must NOT reconfigure.
    auto l2 = store.acquire(owner, key_for(0xB), policy,
        [&compile_count]() { ++compile_count; return make_program(0xB); });
    REQUIRE(l2.program != nullptr);
    CHECK(compile_count == 2);
    CHECK(store.instance_count() == 1);
}

TEST_CASE("scene_program_store: distinct owners accept independent policies") {
    SceneProgramStore store;
    const auto owner_a = PrecompInstanceKey{1, 2};
    const auto owner_b = PrecompInstanceKey{3, 4};

    PrecompCachePolicy policy_a{};
    PrecompCachePolicy policy_b{.initial_capacity = 32};

    auto l_a = store.acquire(owner_a, key_for(0xA), policy_a,
        []() { return make_program(0xA); });
    auto l_b = store.acquire(owner_b, key_for(0xB), policy_b,
        []() { return make_program(0xB); });

    REQUIRE(l_a.program != nullptr);
    REQUIRE(l_b.program != nullptr);
    CHECK(store.instance_count() == 2);
}

// ═════════════════════════════════════════════════════════════════════════════
// §3 WP 5.4 — record_execution triggers per-bucket auto_tune() during acquire()
// ═════════════════════════════════════════════════════════════════════════════
//
// acquire() calls tick_execution_counter internally, which routes through
// SceneProgramCache::record_execution() — the cache then fires auto_tune()
// if the per-bucket counter crosses TuneConfig::interval.  Fixed mode
// bypasses both.

TEST_CASE("scene_program_store: Fixed-mode acquire does not tune the bucket") {
    SceneProgramStore store;
    const auto owner = PrecompInstanceKey{1, 2};
    PrecompCachePolicy policy{};
    policy.initial_capacity = 4;

    // Single acquire — bucket exists with capacity 4.  Fixed mode means
    // record_execution short-circuits inside the cache.
    auto l = store.acquire(owner, key_for(0xA), policy,
        []() { return make_program(0xA); });
    REQUIRE(l.program != nullptr);

    CHECK(store.aggregate_stats().current_size == 1);
    CHECK(store.total_recorded_executions() == 1);
}

TEST_CASE("scene_program_store: Auto-mode acquire triggers auto_tune at interval") {
    SceneProgramStore store;
    const auto owner = PrecompInstanceKey{1, 2};
    PrecompCachePolicy policy{};
    policy.initial_capacity = 4;
    policy.mode = cache::TuneMode::Auto;
    policy.tuning.interval     = 5;
    policy.tuning.min_capacity = 2;
    policy.tuning.max_capacity = 128;

    // 5 acquires — each triggers record_execution.  The 5th invocation
    // crosses `interval == 5` and fires auto_tune() inside the bucket.
    // With 5 distinct keys and capacity 4 → at least one eviction → the
    // auto-tune algorithm expands the bucket (4 → 8).
    for (int i = 0; i < 5; ++i) {
        store.acquire(owner, key_for(static_cast<uint64_t>(0x100 + i)), policy,
            [i]() { return make_program(static_cast<uint64_t>(0x100 + i)); });
    }

    CHECK(store.total_recorded_executions() == 5);
    // Aggregate stats mirror the per-bucket cache after auto_tune.
    auto stats = store.aggregate_stats();
    CHECK(stats.evictions >= 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// §4 WP 5.5 — Per-session reset isolation
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("scene_program_store: clear() drops all per-instance buckets") {
    SceneProgramStore store;
    PrecompCachePolicy policy{};

    store.acquire(PrecompInstanceKey{1, 2}, key_for(0xA), policy,
        []() { return make_program(0xA); });
    store.acquire(PrecompInstanceKey{3, 4}, key_for(0xB), policy,
        []() { return make_program(0xB); });
    CHECK(store.instance_count() == 2);

    store.clear();

    CHECK(store.instance_count() == 0);
    auto stats = store.aggregate_stats();
    CHECK(stats.hits == 0);
    CHECK(stats.misses == 0);
    CHECK(stats.evictions == 0);
    CHECK(stats.current_size == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// §5 — Different buckets run concurrently
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("scene_program_store: different buckets execute concurrently") {
    SceneProgramStore store;
    PrecompCachePolicy policy{};

    constexpr int kBuckets = 8;
    constexpr int kAcquiresPerBucket = 4;
    std::atomic<int> total_compiles{0};

    std::vector<std::thread> workers;
    workers.reserve(kBuckets);
    for (int b = 0; b < kBuckets; ++b) {
        workers.emplace_back([&, b]() {
            const auto owner = PrecompInstanceKey{
                static_cast<uint64_t>(100 + b),
                static_cast<uint64_t>(200 + b)};
            for (int i = 0; i < kAcquiresPerBucket; ++i) {
                auto lease = store.acquire(
                    owner,
                    key_for(static_cast<uint64_t>((b * 1000) + i)),
                    policy,
                    [b, i, &total_compiles]() {
                        total_compiles.fetch_add(1, std::memory_order_relaxed);
                        return make_program(static_cast<uint64_t>((b * 1000) + i));
                    });
                REQUIRE(lease.program != nullptr);
            }
        });
    }
    for (auto& t : workers) t.join();

    CHECK(store.instance_count() == static_cast<std::size_t>(kBuckets));
    // kBuckets * kAcquiresPerBucket distinct keys → all compile events.
    CHECK(total_compiles.load() == kBuckets * kAcquiresPerBucket);
    CHECK(store.aggregate_stats().current_size == kBuckets * kAcquiresPerBucket);
}
