#pragma once

// ── scene_phases.hpp ─────────────────────────────────────────────────────────
//
// Decomposition of the monolithic render_scene_via_graph() into 5 well-scoped
// phases.  Each phase owns a clearly-bounded responsibility and the
// shared state flows through a single SceneRenderContext struct.
//
// Phases:
//   1. FrameReusePhase  — try to reuse previous frame's output (fast path)
//   2. DirtyAnalysisPhase — resolve layers + compute dirty rect
//   3. GraphBuildPhase  — build or reuse compiled render graph
//   4. ExecutePhase     — tile-based or single-pass graph execution
//   5. CommitPhase      — save per-frame state for the next frame
//
// Each phase returns PhaseResult::Done to short-circuit (the caller returns
// the framebuffer already produced) or PhaseResult::Continue to fall through
// to the next phase.
//
// This header is the single source of truth for the cross-phase data flow.

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/dirty_tile_mask.hpp>
#include <chronon3d/core/tile_grid.hpp>
#include <chronon3d/math/raster_utils.hpp>

// Private pipeline headers (same directory)
#include "scene_internal.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

namespace chronon3d::graph::detail {

// ── Timing helper reused by multiple phases ──────────────────────────────────
[[nodiscard]] inline uint64_t to_ms_u64(double ms) {
    return static_cast<uint64_t>(std::llround(std::max(0.0, ms)));
}

enum class PhaseResult {
    Continue,
    Done,
};

/// Shared per-frame state passed between the 5 phases.  All fields are
/// initialised in the orchestrator (render_scene_via_graph) and then
/// populated by the phases in sequence.
struct SceneRenderContext {
    // ── Inputs (immutable for the duration of a frame) ──────────────────
    RenderBackend*              backend{nullptr};
    cache::NodeCache*           node_cache{nullptr};
    const Scene*                scene{nullptr};
    const Camera*               camera{nullptr};
    i32                         width{0};
    i32                         height{0};
    Frame                       frame{0};
    f32                         frame_time{0.0f};
    const RenderSettings*       settings{nullptr};
    const CompositionRegistry*  registry{nullptr};
    video::VideoFrameDecoder*   video_decoder{nullptr};
    float                       fps{30.0f};
    std::string_view            diagnostic_label{};

    // ── Derived state ────────────────────────────────────────────────────
    RenderGraphContext          ctx;
    SoftwareRenderer*           sw_renderer{nullptr};
    LayerResolutionResult       resolved{};

    // ── Phase 2 output ───────────────────────────────────────────────────
    DirtyRectOutput             dirty_out{};

    // ── Phase 3 output ───────────────────────────────────────────────────
    CompiledFrameGraph          compiled{};
    bool                        graph_reused{false};
    bool                        graph_build_skipped{false};

    // ── Phase 4 output ───────────────────────────────────────────────────
    bool                        use_tile_execution{false};
    bool                        fast_path_reuse{false};
    std::shared_ptr<Framebuffer> fb_shared{};

    // ── Dirty analysis counters / diagnostics ────────────────────────────
    double                      dirty_ratio{1.0};
    u64                         dirty_union_area_pixels{0};

    // ── Fingerprints / structure flags ───────────────────────────────────
    bool                        scene_structure_unchanged{false};
    bool                        static_cam_changed{true};
    bool                        scene_is_static{false};
    bool                        active_at_unchanged{false};
    uint64_t                    current_static_fp{0};
    uint64_t                    current_active_at_fp{0};
    uint64_t                    current_structure_fp{0};

    // ── Timing markers (set by the orchestrator, read by the phases) ─────
    std::chrono::steady_clock::time_point t0;
    std::chrono::steady_clock::time_point t_resolve0;
    std::chrono::steady_clock::time_point t_resolve1;
    std::chrono::steady_clock::time_point t_dirty0;
    std::chrono::steady_clock::time_point t_dirty1;
    std::chrono::steady_clock::time_point t_graph0;
    std::chrono::steady_clock::time_point t_graph1;
    std::chrono::steady_clock::time_point t_exec0;
    std::chrono::steady_clock::time_point t_exec1;
};

// ── Phase 1: Frame reuse ─────────────────────────────────────────────────────
// Try to short-circuit the entire render by returning the previous frame's
// framebuffer unchanged.  Covers:
//   - Resolved scene reuse (consecutive / same frame with matching
//     combined fingerprint).
//   - Static scene fast-path (structure + camera + active_at unchanged,
//     consecutive frame).
//   - Empty dirty rect (fast_path_reuse after dirty analysis).
PhaseResult run_frame_reuse_phase(SceneRenderContext& sctx);

// ── Phase 2: Dirty analysis ─────────────────────────────────────────────────
// Resolve layers, compute dirty rect, populate dirty ratio / counters /
// diagnostic info, and detect tile-based execution eligibility.
PhaseResult run_dirty_analysis_phase(SceneRenderContext& sctx);

// ── Phase 3: Graph build ─────────────────────────────────────────────────────
// Wire up ping-pong and transform scratch, then build (or reuse) the
// compiled render graph.  Pre-frame pool preallocation runs here.
// Always continues (no early return from graph build).
void run_graph_build_phase(SceneRenderContext& sctx);

// ── Phase 4: Execute ─────────────────────────────────────────────────────────
// Tile-based execution (when enabled and safe) or traditional single-pass
// graph execution.
// Always continues (no early return from execution).
void run_execute_phase(SceneRenderContext& sctx);

// ── Phase 5: Commit ──────────────────────────────────────────────────────────
// Save per-frame state to the SoftwareRenderer for the next frame: cached
// compiled graph, m_prev_framebuffer, fingerprints, camera, layer bboxes.
void run_commit_phase(SceneRenderContext& sctx);

} // namespace chronon3d::graph::detail
