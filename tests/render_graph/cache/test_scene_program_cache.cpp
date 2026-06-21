// =============================================================================
// test_scene_program_cache.cpp — SceneProgramCache + FrameParameterBlock
//
// Tests from the spec (§B6):
//
// 1. Cache hit — same key → same pointer, compile_count=1, cache_hits=1
// 2. Invalidation — different key → compile_count=2
// 3. Parameter block without residuals — refreshed opacity 0.75, not 0.25
// 4. Capacity stable — after warm-up, capacity doesn't grow over 1000 frames
// 5. LRU eviction — capacity 3: insert A, B, C, access A, insert D → B evicted
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/render_graph/cache/scene_program_cache.hpp>
#include <chronon3d/render_graph/compiler/compiled_scene_program.hpp>
#include <chronon3d/render_graph/pipeline/frame_parameter_block.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>

#include <memory>
using namespace chronon3d;

using namespace chronon3d::graph;
using namespace chronon3d::cache;

namespace {

// ── Helper: build a unique SceneStructureKey ──────────────────────────────
SceneStructureKey make_key(uint64_t topology = 1, int w = 100, int h = 100) {
    return SceneStructureKey{
        .topology_hash      = topology,
        .active_set_hash    = 0,
        .render_options_hash = graph::hash_combine(0, 1),
        .width              = w,
        .height             = h,
        .ssaa_factor        = 1
    };
}

/// A simple "compiler" that returns a minimal valid CompiledSceneProgram.
/// The call counter tracks how many times compilation occurs.
struct TestCompiler {
    int& call_count;

    std::unique_ptr<CompiledSceneProgram> operator()() {
        ++call_count;
        auto program = std::make_unique<CompiledSceneProgram>();

        // Build a minimal valid frame graph.
        auto& graph = program->frame_graph.graph;
        auto src_id = graph.add_node(std::make_unique<TransformNode>(
            Transform{}));
        graph.set_output(src_id);

        program->frame_graph.output = src_id;
        program->frame_graph.levels = {{src_id}};
        program->frame_graph.valid  = true;
        program->valid = true;
        return program;
    }
};

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// §1: Cache hit — same key returns same pointer, compile_count=1
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("scene_program_cache: cache hit returns same pointer") {
    SceneProgramCache cache(8);
    int compile_count = 0;

    auto key = make_key(0xABCD);

    auto first = cache.find_or_compile(key, TestCompiler{compile_count});
    REQUIRE(first != nullptr);

    auto second = cache.find_or_compile(key, TestCompiler{compile_count});

    CHECK(first.get() == second.get()); // same pointer (WP 5.2 — cache returns shared_ptr)
    CHECK(first == second);             // shared_ptr equality compares identity
    CHECK(compile_count == 1);          // compiled only once

    auto s = cache.stats();
    CHECK(s.hits == 1);
    CHECK(s.misses == 1);
}

TEST_CASE("scene_program_cache: multiple cache hits from same key") {
    SceneProgramCache cache(8);
    int compile_count = 0;

    auto key = make_key(42);

    // Compile once.
    auto first = cache.find_or_compile(key, TestCompiler{compile_count});
    REQUIRE(first != nullptr);
    CHECK(compile_count == 1);

    // Many hits.
    for (int i = 0; i < 10; ++i) {
        auto p = cache.find_or_compile(key, TestCompiler{compile_count});
        CHECK(p == first);
    }
    CHECK(compile_count == 1);   // still only one compile

    auto s = cache.stats();
    CHECK(s.hits == 10);         // 10 additional hits
    CHECK(s.misses == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// §2: Invalidation — different key → compile_count=2
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("scene_program_cache: different key triggers recompilation") {
    SceneProgramCache cache(8);
    int compile_count = 0;

    auto key_a = make_key(0x1111);
    auto key_b = make_key(0x2222);

    auto first = cache.find_or_compile(key_a, TestCompiler{compile_count});
    REQUIRE(first != nullptr);
    CHECK(compile_count == 1);

    auto second = cache.find_or_compile(key_b, TestCompiler{compile_count});
    REQUIRE(second != nullptr);

    CHECK(compile_count == 2);          // recompiled for different key
    CHECK(first.get() != second.get()); // different programs

    // Both should now be cached.
    CHECK(cache.find(key_a) == first);
    CHECK(cache.find(key_b) == second);
    CHECK(cache.size() == 2);
}

TEST_CASE("scene_program_cache: same structure key bypasses recompilation") {
    SceneProgramCache cache(8);
    int compile_count = 0;

    auto key = make_key(0xABCD);
    cache.find_or_compile(key, TestCompiler{compile_count});
    CHECK(compile_count == 1);

    // Same key again — no compile.
    cache.find_or_compile(key, TestCompiler{compile_count});
    CHECK(compile_count == 1);

    // Different width → different key → recompile.
    auto key_diff_size = make_key(0xABCD, 200, 200);
    cache.find_or_compile(key_diff_size, TestCompiler{compile_count});
    CHECK(compile_count == 2);
}

TEST_CASE("scene_program_cache: invalid program is not cached") {
    SceneProgramCache cache(8);
    int compile_count = 0;

    // Build a compiler that returns an invalid (empty) program.
    auto invalid_compiler = [&]() -> std::unique_ptr<CompiledSceneProgram> {
        ++compile_count;
        return std::make_unique<CompiledSceneProgram>();  // .valid == false
    };

    auto key = make_key(99);
    auto result = cache.find_or_compile(key, invalid_compiler);
    CHECK(result == nullptr);     // invalid → not cached
    // Note: the compiler was called but the result wasn't stored.
    // The template in the header checks program->valid before storing.
}

// ═════════════════════════════════════════════════════════════════════════════
// §3: Parameter block without residuals
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("frame_parameter_block: no residual values after refresh") {
    FrameParameterBlock block;
    block.warm_up(3);    // 3 binding entries

    // ── Frame 0: simulate setting layer[1].opacity = 0.25 ───────────
    block.begin_frame();
    block[1].opacity = 0.25f;
    block[1].matrix = Mat4{2.0f};  // non-identity
    block[1].refreshed_this_frame = true;
    block.refresh_count = 1;

    CHECK(block[1].opacity == doctest::Approx(0.25f));
    CHECK(block[1].refreshed_this_frame == true);

    // ── Frame 1: refresh only layer[0]. Should NOT see 0.25. ────────
    block.begin_frame();
    block[0].opacity = 0.75f;
    block[0].refreshed_this_frame = true;
    block.refresh_count = 1;

    // Layer[1] was not refreshed this frame — refreshed_this_frame should be false.
    CHECK(block[1].refreshed_this_frame == false);

    // But the actual value should still be 0.25 from the previous write.
    // The test verifies that NO STALE DATA FROM A MISSING REFRESH survives
    // into the next frame WITHOUT explicit overwrite.  However, a proper
    // refresh pipeline always writes to every active binding — residual
    // data only matters when a binding is removed but its slot lingers.
    // The begin_frame() resets refreshed_this_frame so the executor can
    // detect stale slots and skip them.  The value itself is harmless until
    // the slot is re-used, at which point it's overwritten.
    //
    // For the "without residuals" invariant: after a full refresh pass,
    // the parameter block must not contain stale data from a different layer.
    // Since we only wrote to layer[0] this frame, layer[1] retains its old
    // value but is marked not-refreshed.  A well-behaved refresh pipeline
    // writes to EVERY binding every frame, so residuals don't matter.

    // Now simulate a full refresh that overwrites both slots.
    block.begin_frame();
    block[0].opacity = 1.0f;
    block[0].refreshed_this_frame = true;
    block[1].opacity = 0.5f;        // overwritten with new value
    block[1].refreshed_this_frame = true;
    block.refresh_count = 2;

    // After full refresh: no residual 0.25.
    CHECK(block[1].opacity == doctest::Approx(0.5f));
    CHECK(block[1].opacity != doctest::Approx(0.25f));
}

TEST_CASE("frame_parameter_block: opacity refresh without residuals") {
    // Direct test matching the spec: Frame 0 opacity = 0.25,
    // Frame 1 opacity = 0.75, after refresh must be 0.75 exactly.
    FrameParameterBlock block;
    block.warm_up(1);

    // Frame 0
    block.begin_frame();
    block[0].opacity = 0.25f;
    block[0].refreshed_this_frame = true;
    block.refresh_count = 1;

    CHECK(block[0].opacity == doctest::Approx(0.25f));

    // Frame 1 — refresh with new value
    block.begin_frame();
    block[0].opacity = 0.75f;
    block[0].refreshed_this_frame = true;
    block.refresh_count = 2;

    // Must be 0.75, not 0.25.
    CHECK(block[0].opacity == doctest::Approx(0.75f));
    CHECK(block[0].opacity != doctest::Approx(0.25f));
}

// ═════════════════════════════════════════════════════════════════════════════
// §4: Capacity stable — after warm-up, capacity doesn't grow
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("frame_parameter_block: capacity stable after warm-up") {
    FrameParameterBlock block;

    // Initial state.
    CHECK(block.empty());
    CHECK(block.capacity() == 0);

    // Warm-up with 4 entries.
    block.warm_up(4);
    const auto initial_capacity = block.capacity();
    CHECK(block.size() == 4);
    CHECK(initial_capacity >= 4);

    // Simulate 1000 refresh iterations without structural change.
    for (int frame = 0; frame < 1000; ++frame) {
        block.begin_frame();
        for (size_t i = 0; i < block.size(); ++i) {
            block[i].opacity = 0.5f;
            block[i].refreshed_this_frame = true;
        }
        block.refresh_count = block.size();
    }

    // Capacity must remain stable.
    CHECK(block.capacity() == initial_capacity);
    CHECK(block.size() == 4);       // no growth
}

TEST_CASE("frame_parameter_block: warm-up with larger count reallocates") {
    FrameParameterBlock block;

    block.warm_up(2);
    auto first_capacity = block.capacity();

    // Warm-up again with larger count — reallocation expected.
    block.warm_up(8);
    auto second_capacity = block.capacity();

    // New capacity should accommodate 8 entries.
    CHECK(second_capacity >= 8);

    // Now stable again for 1000 frames.
    for (int frame = 0; frame < 1000; ++frame) {
        block.begin_frame();
        for (size_t i = 0; i < block.size(); ++i) {
            block[i].opacity = 1.0f;
            block[i].refreshed_this_frame = true;
        }
    }

    CHECK(block.capacity() == second_capacity);
    CHECK(block.size() == 8);
}

TEST_CASE("frame_parameter_block: warm-up with same count is no-op") {
    FrameParameterBlock block;

    block.warm_up(5);
    auto cap = block.capacity();
    block[0].opacity = 0.3f;

    // warm_up with same count → no reallocation.
    bool no_realloc = block.warm_up(5);
    CHECK(no_realloc == true);
    CHECK(block.capacity() == cap);
    CHECK(block.size() == 5);

    // Data should be cleared by warm_up (spec: clear all state).
    CHECK(block[0].opacity == doctest::Approx(0.0f));
}

// ═════════════════════════════════════════════════════════════════════════════
// §5: LRU eviction — capacity 3
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("scene_program_cache: LRU eviction with capacity 3 (spec)") {
    // Spec: capacity=3, insert A, B, C, access A, insert D → B is evicted.
    // Single shard for deterministic LRU behavior.
    SceneProgramCache cache(3, 1);
    int compile_count = 0;

    auto key_a = make_key(0xA);
    auto key_b = make_key(0xB);
    auto key_c = make_key(0xC);
    auto key_d = make_key(0xD);

    // Insert A, B, C.
    cache.find_or_compile(key_a, TestCompiler{compile_count});  // +1
    cache.find_or_compile(key_b, TestCompiler{compile_count});  // +1
    cache.find_or_compile(key_c, TestCompiler{compile_count});  // +1
    CHECK(compile_count == 3);
    CHECK(cache.size() == 3);

    // Access A (promotes it to MRU head).
    auto access_a = cache.find(key_a);
    REQUIRE(access_a != nullptr);
    CHECK(cache.stats().hits >= 1);

    // Insert D → should evict B (LRU tail).
    cache.find_or_compile(key_d, TestCompiler{compile_count});  // +1
    CHECK(compile_count == 4);
    CHECK(cache.size() == 3);    // still at capacity

    // A and C and D should remain.
    CHECK(cache.contains(key_a));
    CHECK(cache.contains(key_c));
    CHECK(cache.contains(key_d));

    // B should have been evicted.
    CHECK_FALSE(cache.contains(key_b));

    auto s = cache.stats();
    CHECK(s.evictions >= 1);
}

TEST_CASE("scene_program_cache: LRU access order with 4 entries") {
    // Single shard for deterministic LRU behavior.
    SceneProgramCache cache(3, 1);
    int compile_count = 0;

    auto k1 = make_key(1);
    auto k2 = make_key(2);
    auto k3 = make_key(3);
    auto k4 = make_key(4);

    // Fill cache.
    cache.find_or_compile(k1, TestCompiler{compile_count});
    cache.find_or_compile(k2, TestCompiler{compile_count});
    cache.find_or_compile(k3, TestCompiler{compile_count});
    CHECK(cache.size() == 3);

    // Insert k4 → evicts LRU (k1).
    cache.find_or_compile(k4, TestCompiler{compile_count});
    CHECK_FALSE(cache.contains(k1));
    CHECK(cache.contains(k2));
    CHECK(cache.contains(k3));
    CHECK(cache.contains(k4));
}

TEST_CASE("scene_program_cache: LRU touch promotes to MRU") {
    // Single shard for deterministic LRU behavior.
    SceneProgramCache cache(3, 1);
    int compile_count = 0;

    auto k1 = make_key(1);
    auto k2 = make_key(2);
    auto k3 = make_key(3);

    cache.find_or_compile(k1, TestCompiler{compile_count});
    cache.find_or_compile(k2, TestCompiler{compile_count});
    cache.find_or_compile(k3, TestCompiler{compile_count});

    // k1 is LRU (head: k3, then k2, tail: k1).
    // Access k1 → becomes MRU (head: k1, then k3, tail: k2).
    auto touch = cache.find(k1);
    REQUIRE(touch != nullptr);

    // Insert k4 → should evict k2 (now LRU tail), not k1.
    auto k4 = make_key(4);
    cache.find_or_compile(k4, TestCompiler{compile_count});

    CHECK(cache.contains(k1));   // k1 was MRU after access
    CHECK_FALSE(cache.contains(k2));  // k2 was LRU → evicted
    CHECK(cache.contains(k3));
    CHECK(cache.contains(k4));
}

TEST_CASE("scene_program_cache: clear resets everything") {
    SceneProgramCache cache(8);
    int compile_count = 0;

    auto k1 = make_key(1);
    auto k2 = make_key(2);

    cache.find_or_compile(k1, TestCompiler{compile_count});
    cache.find_or_compile(k2, TestCompiler{compile_count});

    CHECK(cache.size() == 2);
    CHECK(compile_count == 2);

    cache.clear();

    CHECK(cache.size() == 0);
    CHECK(cache.stats().hits == 0);
    CHECK(cache.stats().misses == 0);
    CHECK(cache.stats().evictions == 0);
    CHECK_FALSE(cache.contains(k1));
    CHECK_FALSE(cache.contains(k2));
}

TEST_CASE("scene_program_cache: erase removes specific entry") {
    SceneProgramCache cache(8);
    int compile_count = 0;

    auto k1 = make_key(1);
    auto k2 = make_key(2);

    cache.find_or_compile(k1, TestCompiler{compile_count});
    cache.find_or_compile(k2, TestCompiler{compile_count});

    CHECK(cache.size() == 2);

    bool erased = cache.erase(k1);
    CHECK(erased);
    CHECK_FALSE(cache.contains(k1));
    CHECK(cache.contains(k2));
    CHECK(cache.size() == 1);

    // Erase non-existent key returns false.
    CHECK_FALSE(cache.erase(make_key(999)));
}

TEST_CASE("scene_program_cache: set_capacity evicts excess") {
    SceneProgramCache cache(8);
    int compile_count = 0;

    for (int i = 0; i < 8; ++i) {
        cache.find_or_compile(make_key(i), TestCompiler{compile_count});
    }
    CHECK(cache.size() == 8);

    // Shrink to 4 — should evict 4 LRU entries.
    cache.set_capacity(4);

    auto s = cache.stats();
    CHECK(s.evictions >= 4);
    CHECK(cache.size() <= 4);
}

// ═════════════════════════════════════════════════════════════════════════════
// FrameParameterBlock edge cases
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("frame_parameter_block: default construction") {
    FrameParameterBlock block;
    CHECK(block.empty());
    CHECK(block.size() == 0);
    CHECK(block.capacity() == 0);
    CHECK(block.refresh_count == 0);
}

TEST_CASE("frame_parameter_block: begin_frame resets per-frame flags") {
    FrameParameterBlock block;
    block.warm_up(2);

    block[0].refreshed_this_frame = true;
    block[1].opacity = 0.8f;
    block[1].refreshed_this_frame = true;

    block.begin_frame();

    CHECK(block[0].refreshed_this_frame == false);
    CHECK(block[1].refreshed_this_frame == false);
    // Values should remain (begin_frame only resets the 'refreshed' flag)
    CHECK(block[1].opacity == doctest::Approx(0.8f));
}

TEST_CASE("frame_parameter_block: access by index") {
    FrameParameterBlock block;
    block.warm_up(3);

    block[0].opacity = 0.1f;
    block[1].matrix = Mat4{2.0f};
    block[2].effect_blur_radius = 7.0f;
    block[2].has_animated_blur = true;

    CHECK(block[0].opacity == doctest::Approx(0.1f));
    CHECK(block[1].matrix[0][0] == doctest::Approx(2.0f));
    CHECK(block[2].effect_blur_radius == doctest::Approx(7.0f));
    CHECK(block[2].has_animated_blur == true);
}

// ═════════════════════════════════════════════════════════════════════════════
// Telemetry counters integration
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("scene_program_cache: telemetry counters on hit") {
    chronon3d::RenderCounters counters;
    SceneProgramCache cache(8);
    cache.set_counters(&counters);
    int compile_count = 0;

    auto key = make_key(0xCAFE);
    cache.find_or_compile(key, TestCompiler{compile_count});
    CHECK(counters.program_cache_misses.load() == 1);
    CHECK(counters.program_cache_hits.load() == 0);

    cache.find_or_compile(key, TestCompiler{compile_count});
    CHECK(counters.program_cache_hits.load() == 1);
    CHECK(counters.program_cache_misses.load() == 1);
}

TEST_CASE("scene_program_cache: telemetry counters on eviction") {
    chronon3d::RenderCounters counters;
    SceneProgramCache cache(2, 1);  // single shard, cap 2
    cache.set_counters(&counters);
    int compile_count = 0;

    cache.find_or_compile(make_key(100), TestCompiler{compile_count});
    cache.find_or_compile(make_key(200), TestCompiler{compile_count});
    CHECK(counters.program_cache_evictions.load() == 0);

    // Insert third → evicts LRU (key 100).
    cache.find_or_compile(make_key(300), TestCompiler{compile_count});
    CHECK(counters.program_cache_evictions.load() == 1);
    CHECK(counters.program_cache_misses.load() == 3);
}

TEST_CASE("scene_program_cache: telemetry counters without set_counters") {
    // Must not crash when counters pointer is null (default).
    SceneProgramCache cache(8);
    int compile_count = 0;

    auto p = cache.find_or_compile(make_key(99), TestCompiler{compile_count});
    CHECK(p != nullptr);
    CHECK(compile_count == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// Precomp invalidation callback
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("scene_program_cache: eviction callback fires on evict") {
    SceneProgramCache cache(2, 1);  // single shard, cap 2
    int evict_count = 0;
    graph::SceneStructureKey last_evicted;

    cache.set_on_evict([&](const graph::SceneStructureKey& k) {
        ++evict_count;
        last_evicted = k;
    });

    int compile_count = 0;
    auto k1 = make_key(10);
    auto k2 = make_key(20);
    auto k3 = make_key(30);

    cache.find_or_compile(k1, TestCompiler{compile_count});
    cache.find_or_compile(k2, TestCompiler{compile_count});
    CHECK(evict_count == 0);  // no eviction yet

    // Insert third → evicts k1.
    cache.find_or_compile(k3, TestCompiler{compile_count});
    CHECK(evict_count == 1);
    CHECK(last_evicted.topology_hash == k1.topology_hash);
    CHECK(last_evicted.width == k1.width);
}

TEST_CASE("scene_program_cache: eviction callback fires on erase") {
    SceneProgramCache cache(8);
    int evict_count = 0;

    cache.set_on_evict([&](const graph::SceneStructureKey&) {
        ++evict_count;
    });

    int compile_count = 0;
    cache.find_or_compile(make_key(1), TestCompiler{compile_count});
    cache.find_or_compile(make_key(2), TestCompiler{compile_count});
    CHECK(evict_count == 0);

    // Explicit erase should also fire the callback.
    cache.erase(make_key(1));
    CHECK(evict_count == 1);

    cache.erase(make_key(2));
    CHECK(evict_count == 2);
}

TEST_CASE("scene_program_cache: eviction callback fires on set_capacity") {
    SceneProgramCache cache(8, 1);  // single shard
    int evict_count = 0;

    cache.set_on_evict([&](const graph::SceneStructureKey&) {
        ++evict_count;
    });

    int compile_count = 0;
    for (int i = 0; i < 8; ++i) {
        cache.find_or_compile(make_key(i), TestCompiler{compile_count});
    }
    CHECK(evict_count == 0);

    // Shrink → evicts LRU entries.
    cache.set_capacity(4);
    CHECK(evict_count >= 4);
}

TEST_CASE("scene_program_cache: no callback when not set") {
    // Must not crash when callback is not set (null std::function).
    SceneProgramCache cache(2, 1);
    int compile_count = 0;

    cache.find_or_compile(make_key(1), TestCompiler{compile_count});
    cache.find_or_compile(make_key(2), TestCompiler{compile_count});
    cache.find_or_compile(make_key(3), TestCompiler{compile_count});  // evicts #1
    // No crash expected.
    CHECK(cache.size() == 2);
}
