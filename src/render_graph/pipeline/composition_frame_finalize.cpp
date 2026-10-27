#include "composition_frame_internal.hpp"
#include "helpers.hpp"
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/tracing/tracing_categories.hpp>

namespace chronon3d::graph::detail {

std::shared_ptr<Framebuffer> CompositionFrameState::finalize() {
    if (sw_sidecar && !temporal_accumulation) {
sw_sidecar->dirty_telemetry().last_layer_count = layer_count;
    }

    if (ssaa > 1.0f) {
const auto t_down0 = profiling::now();
std::shared_ptr<Framebuffer> out;
{
    CHRONON_TRACE_SCOPE("chronon.effect", "downsample_ssaa");
    // AUDIT-2026-09-09 N-04 — pooled destination: steady-state SSAA
    // borrows the downsample target from the renderer's framebuffer
    // pool (no framebuffer allocation on reuse; one shared owner). Pool-less callers (CLI
    // dry-run, tests) keep the allocating path.
    cache::FramebufferPool* ssaa_pool = nullptr;
    if (sw_sidecar != nullptr) {
        ssaa_pool = sw_sidecar->framebuffer_pool().get();
    }
    out = downsample_fb_pooled(*render_fb, w, h, ssaa_pool);
}
return out;
    }

    return render_fb;
}

}  // namespace chronon3d::graph::detail
