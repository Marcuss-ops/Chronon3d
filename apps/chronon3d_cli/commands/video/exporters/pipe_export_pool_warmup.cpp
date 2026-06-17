#include "../common/pipe_export_session.hpp"

#include <chronon3d/cache/framebuffer_pool.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

namespace {

// Text composition warm-up bundles — pre-allocates size classes used by the
// MinimalistText family so the first frames don't stall on allocation and
// pool exact-hit rate climbs from ~55% to >80% on text-heavy pipelines.
//
//   1920x900  — text-bbox ROI with glow padding (cinem-white radius ~50px).
//               Used by `apply_downsample_blur` clip-bounded regions and by
//               the GlowPipeline ROI accumulator in `build_glow_accumulator`.
//   480x270   — downsample-half heuristic at 4× scale (1920/4 × 1080/4).
//               Used by `BlurStrategy::DownsampleQuarter` for radius > 24.
//
// Both are best-fit reuse candidates when the geometric bbox is small (e.g.
// "FADE UP" centered in a 1920×1080 canvas → ROI is ~1920×900 with side
// margins). Pre-warming them gives exact-hit + best-fit reuse instead of
// fresh allocations on the hot EffectStack path.
void warmup_text_size_classes(cache::FramebufferPool& pool) {
    struct TextSizeClass { int w; int h; int count; const char* label; };
    // Counts tuned against the FramebufferPool default budget (384 MB) so the
    // total preallocation stays well under the cap. Color is float4 = 16 B/px,
    // so e.g. the canvas bucket (1920×1152) costs ~35 MB per buffer. The
    // chosen counts deliver ~273 MB pre-warmed and leave headroom for free
    // allocations during the actual render.
    const TextSizeClass layouts[] = {
        {.w = 1920, .h = 900,  .count = 3, .label = "text-bbox+glow-pad"},
        {.w = 960,  .h = 540,  .count = 3, .label = "downsample-half"},
        {.w = 480,  .h = 270,  .count = 3, .label = "downsample-quarter"},
    };
    for (const auto& cls : layouts) {
        const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(cls.w, cls.h);
        const auto n = pool.preallocate(cache::FramebufferPoolPreallocOptions{
            .width = bw,
            .height = bh,
            .count = cls.count,
            .clear = true,
            .touch_memory = false,
        });
        if (n > 0) {
            spdlog::info("[pool-warm] Pre-allocated {} buffers ({}) bucket {}x{} at startup",
                         n, cls.label, bw, bh);
        }
    }
}

} // namespace

void warmup_pipe_pool(PipeExportSession& session) {
    if (!session.sw_renderer || !session.sw_renderer->framebuffer_pool()) {
        return;
    }

    const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(
        session.canvas_width, session.canvas_height);
    const auto prealloced = session.sw_renderer->framebuffer_pool()->preallocate(
        cache::FramebufferPoolPreallocOptions{
            .width = bw,
            .height = bh,
            .count = 4,
            .clear = true,
            .touch_memory = false,
        });
    if (prealloced > 0) {
        spdlog::info("[pool-warm] Pre-allocated {} canvas buffers ({}x{} bucket) at startup",
                     prealloced, bw, bh);
    }

    // Pre-warm the text-composition ROI + downsample size classes.
    warmup_text_size_classes(*session.sw_renderer->framebuffer_pool());
}

} // namespace chronon3d::cli
