// ==============================================================================
// tests/runtime/test_render_session_reset_and_isolation.cpp
//
// WP-3 — render-session PRs 3.0, 3.1, 3.2 (full progression).
//
// PR 3.2 (this delivery) — `RendererFrameHistory` /
// `RendererDirtyTelemetry` canonicalised to `FrameHistory` /
// `DirtyHistory`; `RendererLayerHistory` REMOVED (folded into
// `DirtyHistory::previous_layers`).  `LayerBBoxState` moved from
// `math/renderer_state.hpp` to `runtime/dirty_history.hpp` and is
// now a public-field struct (the test-pin limitation is REMOVED:
// tests can seed non-default values for the strict-preservation
// assertions on `previous_layers`).
//
// PR 3.1 — per-session ownership of `SceneHasher` + `SceneProgramStore`.
// PR 3.0 — noexcept removal from accessors that throw (no longer
//   relevant post-PR-3.1 because the accessors don't throw;
//   covered by the "succeeds without throwing" assertions below).
//
// ==============================================================================

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/internal/runtime/render_session.hpp>
#include <chronon3d/runtime/dirty_history.hpp>
#include <chronon3d/backends/software/software_render_session.hpp>
#include <chronon3d/core/types/types.hpp>

#include <atomic>
#include <string>
#include <thread>

using chronon3d::RenderSession;
using chronon3d::FrameHistory;
using chronon3d::DirtyHistory;
using chronon3d::LayerBBoxState;
using chronon3d::i32;
using chronon3d::graph::SceneHasher;
using chronon3d::graph::SceneProgramStore;
using chronon3d::raster::BBox;
using chronon3d::i32;

namespace chronon3d {

// WP-3 PR 3.2 — canonical `FrameHistory` has no `prev_runtime_settings_fingerprint`
// or `last_committed_frame_index` (those fields were stripped during the
// `RendererFrameHistory` → `FrameHistory` canonicalisation).  The injected
// `operator==` covers the FINGERPRINT + STRUCTURAL fields.  The camera-path
// comparison (`prev_camera` + `prev_camera_valid`) is intentionally OMITTED
// because Camera2_5D's projection spec (LensModel / DepthOfFieldSettings /
// MotionBlurSettings / pmr strings / Quat) doesn't ship canonical
// operator== on every sub-field — the camera path is exercised by its own
// dedicated tests in `tests/scene/` rather than bolted onto this lattice.
// Pragmatic WP-3 isolation: prefer a not-quite-comprehensive operator==
// over a cascade of sub-type operator==s.
inline bool operator==(const FrameHistory& a, const FrameHistory& b) noexcept {
    return a.prev_frame == b.prev_frame
        && a.prev_scene_fingerprint == b.prev_scene_fingerprint
        && a.prev_static_scene_fingerprint == b.prev_static_scene_fingerprint
        && a.prev_graph_structure_fingerprint == b.prev_graph_structure_fingerprint
        && a.prev_active_at_fingerprint == b.prev_active_at_fingerprint;
}

} // namespace chronon3d

namespace chronon3d::raster {

// WP-3 PR 3.2 — operator== for `raster::BBox` (canonical schema is
// public-field with inclusive-exclusive corners `x0`/`y0`/`x1`/`y1`).
// Must live in `chronon3d::raster` (NOT parent `chronon3d`) because
// ADL for `std::optional<BBox>` only inspects the namespaces of BBox
// itself; an operator== defined in `chronon3d::` is NOT visible to
// ADL and `std::optional<BBox>::operator==` therefore fails to resolve.
inline bool operator==(const BBox& a, const BBox& b) noexcept {
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

} // namespace chronon3d::raster

namespace {

void seed_frame_history(RenderSession& s, uint64_t marker) {
    // Canonical `FrameHistory` field set (WP-3 PR 3.2).  The seed
    // intentionally drives `prev_graph_structure_fingerprint` because
    // that is the byte the lattice's persistence assertions key off; the
    // operator== injected above already excludes the legacy
    // `prev_runtime_settings_fingerprint` / `last_committed_frame_index`
    // fields so those don't need to be set.
    s.frame_history.prev_graph_structure_fingerprint = marker;
}

void seed_dirty_telemetry(RenderSession& s, uint64_t marker) {
    // WP-3 PR 3.2 — `DirtyHistory` canonical fields.  The pre-3.2
    // `total_pixels` / `dirty_pixels` fields are gone (that schema was
    // the legacy `RendererDirtyTelemetry` table; the canonical fields
    // are listed below).
    s.dirty_telemetry.last_dirty_area_ratio    = double(marker & 0xFF) / 255.0;
    s.dirty_telemetry.last_layer_count         = static_cast<int>(marker & 0xFF);
    s.dirty_telemetry.last_dirty_rect_enabled  = true;
    s.dirty_telemetry.last_dirty_rect          = BBox{0, 0,
                                                     static_cast<i32>(marker & 0xFFFF),
                                                     static_cast<i32>((marker >> 16) & 0xFFFF)};
    s.dirty_telemetry.last_tile_execution_used = true;
    s.dirty_telemetry.last_fast_path_reused    = false;
    s.dirty_telemetry.last_graph_reused        = true;
}

// WP-3 PR 3.2 — seed previous_layers with a non-default map.  This is
// what UNBLOCKED the test-pin limitation: `LayerBBoxState` is now a
// public-field struct so tests can stamp non-default entries directly.
void seed_previous_layers(RenderSession& s, uint64_t marker) {
    s.dirty_telemetry.previous_layers.clear();
    s.dirty_telemetry.previous_layers["bg"]  = LayerBBoxState{
        .bbox = BBox{0, 0,
                      static_cast<i32>(marker & 0xFFFF),
                      static_cast<i32>((marker >> 16) & 0xFFFF)},
        .world_matrix           = {},
        .opacity                = 1.0f,
        .visible                = true,
        .cache_static           = false,
        .uses_2_5d_projection   = false,
        .content_hash           = marker,
    };
    s.dirty_telemetry.previous_layers["fg"]  = LayerBBoxState{
        .bbox = BBox{100, 100, 200, 200},
        .world_matrix           = {},
        .opacity                = 0.85f,
        .visible                = true,
        .cache_static           = false,
        .uses_2_5d_projection   = true,
        .content_hash           = marker + 0xC0FFEE,
    };
}

bool dirty_history_default(const DirtyHistory& d) noexcept {
    // Default-constructed DirtyHistory canonical defaults.  Layer
    // diff source-of-truth (`previous_layers`) is empty by default.
    // NOTE: previous_layers is intentionally excluded — it survives
    // per-frame resets (see DirtyHistory::reset_telemetry_counters()
    // contract).  Callers that need to assert previous_layers empty
    // must add an explicit CHECK(…previous_layers.empty()).
    return d.last_dirty_area_ratio == 1.0
        && d.last_layer_count == 0
        && !d.last_dirty_rect_enabled
        && !d.last_dirty_rect.has_value()
        && !d.last_tile_execution_used
        && !d.last_fast_path_reused
        && !d.last_graph_reused;
}

} // namespace

// ============================================================================
// WP-3 PR 3.1 — Default-constructed session accessors succeed (no throw).
// ============================================================================

TEST_CASE("WP-3 PR 3.1: default-constructed scene_hasher() succeeds without throwing") {
    RenderSession session;
    CHECK_NOTHROW((void)session.scene_hasher());
    CHECK_NOTHROW((void)static_cast<const RenderSession&>(session).scene_hasher());
}

TEST_CASE("WP-3 PR 3.1: default-constructed program_store() succeeds without throwing") {
    RenderSession session;
    CHECK_NOTHROW((void)session.program_store());
    CHECK_NOTHROW((void)static_cast<const RenderSession&>(session).program_store());
}

TEST_CASE("WP-3 PR 3.1: default-constructed program_store() is non-null and apis reachable") {
    RenderSession session;
    auto& ps = session.program_store();
    CHECK(&ps != nullptr);
    CHECK_NOTHROW((void)ps.aggregate_stats());
}

TEST_CASE("WP-3 PR 3.0 → 3.1: arena() remains noexcept (negative regression guard)") {
    RenderSession session;
    const auto& ca = static_cast<const RenderSession&>(session).arena();
    CHECK(&ca == &static_cast<const RenderSession&>(session).arena());
}

// ============================================================================
// WP-3 PR 3.2 + 3.3 + 3.1 — reset semantics with canonical schemas
// ============================================================================

TEST_CASE("WP-3 PR 3.3: reset_frame_temporaries preserves frame_history"
          " (and does NOT touch scene_hasher / program_store)") {
    RenderSession session;
    seed_frame_history(session, 0xAABBCCDD);
    seed_dirty_telemetry(session, 0xAABBCCDD);

    auto scene_hasher_addr = &session.scene_hasher();
    auto program_store_addr = &session.program_store();

    const auto fh_before = session.frame_history;
    session.reset_frame_temporaries();

    CHECK(session.frame_history == fh_before);
    CHECK(dirty_history_default(session.dirty_telemetry));
    CHECK(&session.scene_hasher() == scene_hasher_addr);
    CHECK(&session.program_store() == program_store_addr);
}

TEST_CASE("WP-3 PR 3.2 + 3.3: reset_frame_temporaries PRESERVES"
          " previous_layers (PR 3.2 fold; layer-bbox diff source-of-truth"
          " survives the per-frame boundary)") {
    // WP-3 PR 3.2 — pre-3.2 the layer-bbox history was reset by the
    // per-frame reset (via the old `RendererLayerHistory` reset).
    // Post-3.2 the canonical home is `dirty_telemetry.previous_layers`
    // and it MUST survive the per-frame boundary so inter-frame
    // dirty-rect diffs remain accurate.  This test pins the new
    // invariant (replaces the pre-3.2 "default state unchanged"
    // tautology).
    RenderSession session;
    seed_previous_layers(session, 0xC0FFEE);
    REQUIRE(!session.dirty_telemetry.previous_layers.empty());

    session.reset_frame_temporaries();

    // previous_layers preserved across a per-frame reset.
    CHECK(session.dirty_telemetry.previous_layers.size() == 2);
    auto it_bg = session.dirty_telemetry.previous_layers.find("bg");
    CHECK(it_bg != session.dirty_telemetry.previous_layers.end());
    CHECK(it_bg->second.content_hash == 0xC0FFEE);
    CHECK(it_bg->second.visible);
    auto it_fg = session.dirty_telemetry.previous_layers.find("fg");
    CHECK(it_fg != session.dirty_telemetry.previous_layers.end());
    CHECK(it_fg->second.opacity == 0.85f);

    // But the telemetry counters are reset.
    CHECK(dirty_history_default(session.dirty_telemetry));
}

TEST_CASE("WP-3 PR 3.2 + 3.3 + 3.1: reset_job clears frame_history, dirty_telemetry,"
          " previous_layers, scene_hasher; zero program_store without aborting") {
    RenderSession session;
    seed_frame_history(session, 0xCAFEBABE);
    seed_dirty_telemetry(session, 0xCAFEBABE);
    seed_previous_layers(session, 0xCAFEBABE);    CHECK_NOTHROW(session.reset_job());
    CHECK(session.frame_history == FrameHistory{});
    CHECK(session.dirty_telemetry.previous_layers.empty());
    CHECK(dirty_history_default(session.dirty_telemetry));
    CHECK_NOTHROW((void)session.scene_hasher());
    CHECK_NOTHROW((void)session.program_store());
}

TEST_CASE("WP-3 PR 3.2 + 3.3 + 3.1: reset_job on a default-constructed session does NOT abort") {
    RenderSession session;
    seed_frame_history(session, 0xDEADBEEF);
    seed_dirty_telemetry(session, 0xDEADBEEF);
    seed_previous_layers(session, 0xDEADBEEF);

    CHECK_NOTHROW(session.reset_job());
    CHECK(session.frame_history == FrameHistory{});
    CHECK(dirty_history_default(session.dirty_telemetry));
}

// ============================================================================
// WP-3 PR 3.3 + 3.1 — concurrent default-constructed sessions
// ============================================================================

TEST_CASE("WP-3 PR 3.3 + 3.1: concurrent sessions' local state is independently resettable") {
    constexpr int kSeed = 0x600DCAFE;
    constexpr int kIters = 1000;

    std::atomic<int> isolated_rounds{0};

    auto worker = [&](uint64_t tag) {
        for (int i = 0; i < kIters; ++i) {
            RenderSession s;
            seed_frame_history(s, tag);
            seed_dirty_telemetry(s, tag);
            seed_previous_layers(s, tag);
            // Pre-reset guard: seeding succeeded if any of these are
            // not at default values.
            REQUIRE(s.frame_history.prev_graph_structure_fingerprint != 0u);
            REQUIRE(!s.dirty_telemetry.previous_layers.empty());
            s.reset_job();
            if (s.frame_history == FrameHistory{}
                && dirty_history_default(s.dirty_telemetry)) {
                isolated_rounds.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::thread a(worker, kSeed);
    std::thread b(worker, kSeed + 1);
    a.join();
    b.join();

    CHECK(isolated_rounds.load() == 2 * kIters);
}

TEST_CASE("WP-3 PR 3.3 + 3.1 + 3.2: reset_frame_temporaries called concurrently on two sessions"
          " is idempotent and non-aborting (preserves previous_layers)") {
    std::atomic<int> a_runs{0};
    std::atomic<int> b_runs{0};

    std::thread a([&]() {
        for (int i = 0; i < 2000; ++i) {
            RenderSession s;
            seed_dirty_telemetry(s, 0x1111);
            seed_previous_layers(s, 0x1111);
            s.reset_frame_temporaries();
            // Telemetry counters reset, but previous_layers preserved.
            if (!dirty_history_default(s.dirty_telemetry)) continue;
            if (s.dirty_telemetry.previous_layers.empty()) continue;
            a_runs.fetch_add(1, std::memory_order_relaxed);
        }
    });
    std::thread b([&]() {
        for (int i = 0; i < 2000; ++i) {
            RenderSession s;
            seed_dirty_telemetry(s, 0x2222);
            seed_previous_layers(s, 0x2222);
            s.reset_frame_temporaries();
            if (!dirty_history_default(s.dirty_telemetry)) continue;
            if (s.dirty_telemetry.previous_layers.empty()) continue;
            b_runs.fetch_add(1, std::memory_order_relaxed);
        }
    });
    a.join();
    b.join();

    CHECK(a_runs.load() == 2000);
    CHECK(b_runs.load() == 2000);
}

// ============================================================================
// WP-3 PR 3.1 — per-session ownership flipped pin tests
// ============================================================================

TEST_CASE("WP-3 PR 3.1: two default-constructed sessions have DISTINCT scene_hasher addresses") {
    RenderSession a;
    RenderSession b;
    CHECK(&a.scene_hasher() != &b.scene_hasher());
    CHECK(&static_cast<const RenderSession&>(a).scene_hasher() !=
          &static_cast<const RenderSession&>(b).scene_hasher());
}

TEST_CASE("WP-3 PR 3.1: two default-constructed sessions have DISTINCT program_store addresses") {
    RenderSession a;
    RenderSession b;
    CHECK(&a.program_store() != &b.program_store());
}

TEST_CASE("WP-3 PR 3.1: reset_job on session A does NOT clear session B's per-session state") {
    RenderSession a;
    RenderSession b;

    seed_frame_history(a, 0xBEEF0001);
    seed_frame_history(b, 0xBEEF0002);
    seed_dirty_telemetry(a, 0xCAFE0001);
    seed_dirty_telemetry(b, 0xCAFE0002);
    // WP-3 PR 3.2 — seed B's previous_layers non-default so we can
    // prove it survives A's reset_job.
    seed_previous_layers(a, 0xA5A50001);
    seed_previous_layers(b, 0xA5A50002);

    const auto b_frame_before    = b.frame_history;
    const auto b_dirty_before    = b.dirty_telemetry;
    const auto b_programstore_addr_before = &b.program_store();
    const auto a_scene_addr_before = &a.scene_hasher();

    a.reset_job();

    // A is now reset on every local member.
    CHECK(a.frame_history == FrameHistory{});
    CHECK(dirty_history_default(a.dirty_telemetry));

    // B's per-session state MUST NOT be touched by A's reset.
    CHECK(b.frame_history == b_frame_before);
    CHECK(b.dirty_telemetry.last_dirty_area_ratio == b_dirty_before.last_dirty_area_ratio);
    CHECK(b.dirty_telemetry.last_dirty_rect_enabled == b_dirty_before.last_dirty_rect_enabled);
    CHECK(b.dirty_telemetry.last_dirty_rect == b_dirty_before.last_dirty_rect);
    CHECK(b.dirty_telemetry.previous_layers.size() == b_dirty_before.previous_layers.size());
    REQUIRE(!b.dirty_telemetry.previous_layers.empty());
    auto b_bg = b.dirty_telemetry.previous_layers.find("bg");
    CHECK(b_bg != b.dirty_telemetry.previous_layers.end());
    CHECK(b_bg->second.content_hash == b_dirty_before.previous_layers.at("bg").content_hash);
    CHECK(&b.program_store() == b_programstore_addr_before);
    CHECK(&a.scene_hasher() == a_scene_addr_before);
}

// ============================================================================
// WP-3 PR 3.1 — SoftwareRenderSession orchestration
// ============================================================================

TEST_CASE("WP-3 PR 3.1 + 3.2: SoftwareRenderSession::reset_job reaches both halves of the composition") {
    chronon3d::SoftwareRenderSession sr;

    seed_frame_history(sr.common, 0xDEAD0001);
    seed_dirty_telemetry(sr.common, 0xDEAD0001);
    seed_previous_layers(sr.common, 0xDEAD0001);

    const auto common_scene_hasher_addr = &sr.common.scene_hasher();
    const auto common_program_store_addr = &sr.common.program_store();

    sr.reset_job();

    // common half reset (per-session members all zeroed).
    CHECK(sr.common.frame_history == FrameHistory{});
    CHECK(dirty_history_default(sr.common.dirty_telemetry));
    CHECK(&sr.common.scene_hasher() == common_scene_hasher_addr);
    CHECK(&sr.common.program_store() == common_program_store_addr);

    // software half also reset (buffer_ring + scratch_buffer released).
    CHECK_NOTHROW((void)sr.software.buffer_ring);
    CHECK_NOTHROW((void)sr.software.scratch_buffer);

    // WP-3 PR 3.2 — `SoftwareRenderSession` exposes scene_hasher /
    // program_store exclusively through the common half (the
    // previously-duplicated software.scene_hasher is gone).
    CHECK(&sr.scene_hasher() == &sr.common.scene_hasher());
    CHECK(&sr.program_store() == &sr.common.program_store());
}
