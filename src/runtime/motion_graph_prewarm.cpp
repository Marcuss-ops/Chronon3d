// motion_graph_prewarm.cpp — Implementation of prewarm_motion_graph
// orchestrator.
//
// v1.0 contract:
//   * No throws.
//   * Idempotent: same Scene + same Options -> same counts + same notes.
//   * notes string is always emitted (carries the stub warning) so callers
//     reading `text_layers_touched` are not misled: v1.0 ships candidate
//     counts only — no actual TextLayoutCache (or renderer_warmup pool)
//     is primed by this orchestrator. Real wiring lands in v1.1 alongside
//     the Audio pre-warm hook and the scene-aware renderer_warmup surface.

#include <chronon3d/runtime/motion_graph_prewarm.hpp>

#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>

#include <algorithm>
#include <chrono>
#include <sstream>
#include <string>

// Forward-declared stub for v1.0 (real hook lands in v1.1).
namespace chronon3d::text {
    [[nodiscard]] inline bool text_layer_prewarm_stub(LayerKind k) noexcept {
        (void)k;
        return true;
    }
}

namespace chronon3d {

MotionPrewarmResult prewarm_motion_graph(const Scene& scene,
                                         const MotionPrewarmOptions& opts) {
    MotionPrewarmResult result;
    const auto t0 = std::chrono::steady_clock::now();

    // ── (1) Total layer count (candidate, not proof of pre-warm).
    result.layers_touched = scene.layers().size();

    // ── (2) Text-layer candidate count. The stub is a no-op; the field
    //         records "we considered these layers for text pre-warm" so
    //         callers can see scope even when no cache is primed.
    if (opts.include_text_run_layout) {
        size_t text_count = 0;
        for (const auto& layer : scene.layers()) {
            if (layer.is_text()) {
                chronon3d::text::text_layer_prewarm_stub(layer.kind);
                ++text_count;
            }
        }
        result.text_layers_touched = text_count;
    }

    // ── (3) Render-graph stub: v1.1 wires renderer_warmup(Scene&) here.
    if (opts.include_render_graph) {
        // Silent placeholder for the camera-pool warm-up that lands later.
    }

    // ── (4) ALWAYS emit v1.0 stub warning in notes (regardless of
    //         verbose_log). The contract is that callers see the truth.
    {
        std::ostringstream oss;
        oss << "v1.0 stubs: text=" << result.text_layers_touched
            << " is a CANDIDATE count (no TextLayoutCache primed); "
            << "renderer=" << (opts.include_render_graph ? "on" : "off")
            << " is a stub (no framebuffer pool primed)";
        result.notes = oss.str();
    }

    // ── (5) Optional verbose breakdown on top of the stub warning.
    if (opts.verbose_log) {
        std::ostringstream oss;
        oss << "\n[layers=" << result.layers_touched
            << " text=" << result.text_layers_touched
            << " frame=" << opts.target_frame.value
            << " include_render_graph=" << opts.include_render_graph
            << " include_text_run_layout=" << opts.include_text_run_layout
            << "]";
        result.notes += oss.str();
    }

    result.ok = true;
    const auto t1 = std::chrono::steady_clock::now();
    result.elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                            t1 - t0).count();
    return result;
}

} // namespace chronon3d
