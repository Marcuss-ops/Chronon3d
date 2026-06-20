// motion_graph_prewarm.hpp — Public orchestrator entry point for the
// motion-graph pre-warm pass.
//
// Goal: collapse the existing pre-warm hooks (text-run layout cache,
// render-graph warm-up, future audio/video pre-warm) into a single
// idempotent facade so callers (CLI, exporter, headless benchmark) can
// prime the motion pipeline with one call.
//
// v1.0 contract:
//   * prewarm_motion_graph() walks the Scene and, depending on flags,
//     touches each layer's pre-warm path. Idempotent — calling twice for
//     the same Scene/Options yields the same return struct (allowing for
//     epsilon-difference on elapsed_us).
//   * Returns a struct describing which subsystems ran and how long.
//   * Side effects: minor — populates internal caches (TextLayoutCache
//     for text layers). Safe to call repeatedly.
//
// Out of scope for v1.0:
//   * Audio pre-warm (no Audio-typed path exists yet; added in v1.1).
//   * Cross-frame dependency tracking (target_frame is captured but not
//     used to schedule future-frame caches yet).

#pragma once

#include <chronon3d/core/types/types.hpp>
#include <string>

namespace chronon3d {

class Scene;

// ── MotionPrewarmOptions: per-call knobs. All default-on. ─────────────
struct MotionPrewarmOptions {
    Frame target_frame{0};

    /// Render-graph warm-up (framebuffer pool, internal caches).
    /// Currently a no-op stub for v1.0 — wired to runtime internals in v1.1.
    bool   include_render_graph{true};

    /// Text-run layout pre-warm. Calls
    /// chronon3d::text::prewarm_text_run_layout_for_frame for each text
    /// layer (kind == LayerKind::Text) when enabled.
    bool   include_text_run_layout{true};

    /// Emit diagnostic notes into the result instead of discarding them.
    bool   verbose_log{false};
};

// ── MotionPrewarmResult: what the orchestrator observed. ──────────────
struct MotionPrewarmResult {
    bool        ok{false};
    size_t      layers_touched{0};
    size_t      text_layers_touched{0};
    i64         elapsed_us{0};
    std::string notes;  // empty unless verbose_log == true
};

// ── prewarm_motion_graph: idempotent orchestrator. Returns counts. ─────
//
// Behaviour:
//   * empty Scene               -> ok=true, layers_touched=0
//   * Scene with N text layers  -> text_layers_touched == N when
//                                  include_text_run_layout==true
//   * Scene with N total layers -> layers_touched == N
//   * Throws nothing. Returns by value.
MotionPrewarmResult prewarm_motion_graph(const Scene& scene,
                                         const MotionPrewarmOptions& opts = {});

} // namespace chronon3d
