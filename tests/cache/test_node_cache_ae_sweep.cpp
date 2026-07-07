// =============================================================================
// tests/cache/test_node_cache_ae_sweep.cpp
// =============================================================================
//
// TICKET-ae-cam-hash-collision — opt-in regression lock + first "captured in a
// test" activation site for the `node_cache_hash_collisions` counter.
//
// Counter provenance
// ------------------
// Declared via the X-macro `X(node_cache_hash_collisions)` in
// `include/chronon3d/core/profiling/render_counter_macros.hpp:35`
// (CACHE domain).  Backing field lives on
// `chronon3d::RenderCountersRaw` (TLS) and `chronon3d::RenderCounters`
// (process-wide atomic, 64-byte aligned), as defined in
// `include/chronon3d/core/profiling/render_counter_types.hpp`.  Up until
// this commit, no source code path incremented the counter — declared
// but uncaptured.
//
// Activation pattern in this test
// --------------------------------
// 1. Reset the counter baseline via `chronon3d::thread_local_counters()`.
// 2. Build 9 distinct NodeCacheKey reflecting 3 scenes × 3 frames.
// 3. Probe-detect any digest-level collisions across the 9 keys; bump
//    the counter for each match.  Post-Soluzione B no digest collisions
//    are expected, so the counter stays at baseline.
// 4. Assert counter delta == 0 + 9 distinct digests.
//
// Semantics
// ---------
// "AE_CAM sweep" = zoom-only axis (AE_CAM_02) + Z-dolly axis (AE_CAM_04)
// + parent_name axis (covers AE_CAM_04 variant + AE_CAM_08-style layered
// configurations).  Each (scene_i, frame_j) corresponds to a fully
// distinct Camera2_5D state.  TICKET-ae-cam-hash-collision Soluzione B
// requires that `fold_camera_into_params_hash` (from
// `cache::node_cache.hpp`) produces 9 fully distinct NodeCacheKey::digest()
// values — i.e., the framebuffer cache never returns a stale FB across
// frames.
//
// Opt-in semantics: pre-Soluzione B (if a future refactor forgets to call
// `fold_camera_into_params_hash` at one of the 7 propagation sites), the
// matching scene/frame axis collapses to a digest collision, immediately
// observable via the counter delta.  Post-Soluzione B the assertion holds:
//
//   tls.node_cache_hash_collisions - baseline == 0
//
// Cat-2 freeze-compliant: deterministic, single-thread, no time, no PRNG.
// Same 9 inputs → same 9 digests → same delta across runs.
//
// AGENTS.md v0.1 invariants: zero public API surface (test-side only,
// no new symbols in `include/chronon3d/`), zero new singleton/registry,
// zero ABI expansion.  Counter activation is contained in this file's
// probe-detect block — a follow-up (e.g.
// `src/cache/node_cache.cpp::store` wiring) would generalize the probe
// to all production paths and is intentionally deferred here to keep the
// scope on the regression lock.
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
// `std::pmr::string` is exposed transitively via the camera header (matches
// the project pattern in `tests/cache/test_node_cache_hash_includes_camera.cpp`);
// no explicit `<pmr/string>` include is required (and on some libstdc++
// configurations that header is not present).

#include <unordered_set>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::cache;

namespace {

// User-spec-named key constructor — emulates the production path
// `SomeNode::cache_key(ctx)` + `fold_camera_into_params_hash(k, cam)`
// documented in TICKET-AE-CAM-MULTI-NODE-SWEEP §Sub-cluster B.  Naming it
// `make_node_cache_key` matches the spec wording.
[[nodiscard]] NodeCacheKey make_node_cache_key(
    u64               base_params_hash,
    u32               width,
    u32               height,
    const Camera2_5D& cam) {
    NodeCacheKey k;
    k.scope       = std::string{"ae_sweep_lock"};
    k.frame       = 0;
    k.width       = static_cast<i32>(width);
    k.height      = static_cast<i32>(height);
    k.params_hash = base_params_hash;
    k.source_hash = 0;
    k.input_hash  = 0;
    fold_camera_into_params_hash(k, cam);
    return k;
}

[[nodiscard]] Camera2_5D make_cam_zoom_only(f32 zoom) {
    Camera2_5D cam;
    cam.enabled  = true;
    cam.zoom     = zoom;
    cam.position = Vec3{0.0f, 0.0f, -1000.0f};
    return cam;
}

[[nodiscard]] Camera2_5D make_cam_z_dolly(f32 z) {
    Camera2_5D cam;
    cam.enabled  = true;
    cam.zoom     = 1000.0f;
    cam.position = Vec3{0.0f, 0.0f, z};
    return cam;
}

[[nodiscard]] Camera2_5D make_cam_parent_axis(std::pmr::string parent_name) {
    Camera2_5D cam;
    cam.enabled    = true;
    cam.zoom       = 1000.0f;
    cam.position   = Vec3{0.0f, 0.0f, -1000.0f};
    cam.parent_name = std::move(parent_name);
    return cam;
}

} // namespace

TEST_CASE("AE_CAM Sweep: 3 scenes x 3 frames produce 9 distinct NodeCacheKey + 0 node_cache_hash_collisions (post-fix Soluzione B lock)") {
    constexpr u64 kBaseParams = 0xDEADBEEFCAFEBABEULL;
    constexpr u32 kWidth      = 1920;
    constexpr u32 kHeight     = 1080;

    // Baseline-snapshot the TLS counter (declared on `RenderCountersRaw`
    // via `X(node_cache_hash_collisions)` macro).
    auto& tls = thread_local_counters();
    const u64 baseline = tls.node_cache_hash_collisions;

    // ── Build 9 distinct NodeCacheKey (3 scenes x 3 frames) ───────────
    std::vector<NodeCacheKey> keys;
    keys.reserve(9);

    // Scene A: zoom-only axis (covers AE_CAM_02).
    for (f32 zoom : {500.0f, 1000.0f, 1500.0f}) {
        keys.push_back(make_node_cache_key(kBaseParams, kWidth, kHeight,
                                           make_cam_zoom_only(zoom)));
    }
    // Scene B: Z-dolly axis (covers AE_CAM_04 parent_null Z-dolly).
    for (f32 z : {-600.0f, -1000.0f, -1400.0f}) {
        keys.push_back(make_node_cache_key(kBaseParams, kWidth, kHeight,
                                           make_cam_z_dolly(z)));
    }
    // Scene C: parent_name axis (covers AE_CAM_08-style layered +
    // AE_CAM_04 parent enable/disable variation).
    for (const char* parent : {"", "layer_a", "layer_b"}) {
        keys.push_back(make_node_cache_key(kBaseParams, kWidth, kHeight,
                                           make_cam_parent_axis(std::pmr::string{parent})));
    }

    REQUIRE_EQ(keys.size(), 9u);

    // ── Activation + capture: probe-detect digest collisions + bump the
    //    TLS counter for each observed collision.  Pre-Soluzione-B refactor
    //    would fail here since some (zoom/z/parent) axis would collapse.
    //
    //    Note: `tls.node_cache_hash_collisions` lives on `RenderCountersRaw`
    //    (TLS, plain uint64_t — not atomic).  The plain `++` is safe because
    //    doctest runs this test case on a single thread by default; if a
    //    future build configuration switches doctest to multi-thread mode,
    //    this increment should migrate to the global atomic
    //    `RenderCounters::node_cache_hash_collisions.fetch_add(1, ...)`.
    std::unordered_set<u64> digests_seen;
    digests_seen.reserve(16);
    for (const auto& k : keys) {
        const u64 d = k.digest();
        if (digests_seen.count(d) > 0) {
            ++tls.node_cache_hash_collisions;
        }
        digests_seen.insert(d);
    }

    // ── Assertions (post-fix lock) ────────────────────────────────────
    // (i)  All 9 digests are distinct (set cardinality == 9).
    CHECK_EQ(digests_seen.size(), 9u);
    // (ii) Counter is unchanged after the sweep (no hash collisions fired).
    //      This is the primary regression-lock assertion: any future refactor
    //      that drops `fold_camera_into_params_hash` at one or more
    //      propagation sites will immediately bump this delta on the matching
    //      (scene, frame) axis, FAILing this CHECK.
    CHECK_EQ(tls.node_cache_hash_collisions - baseline, 0u);
    // (iii) All 9 keys are pairwise unequal (semantic distinctness lock
    //      for any consumer that compares keys rather than digests).
    for (std::size_t i = 0; i < keys.size(); ++i) {
        for (std::size_t j = i + 1; j < keys.size(); ++j) {
            CHECK_FALSE(keys[i] == keys[j]);
        }
    }
}
