#include "../common/pipe_export_pipeline.hpp"

#include <chronon3d/cache/framebuffer_pool.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {
namespace {

void warmup_text_size_classes(cache::FramebufferPool& pool) {
    struct TextSizeClass { int w; int h; size_t count; const char* label; };
    const TextSizeClass layout[] = {
        {.w = 1920, .h = 900,  .count = 3, .label = "text-bbox+glow-pad"},
        {.w = 960,  .h = 540,  .count = 3, .label = "downsample-half"},
        {.w = 480,  .h = 270,  .count = 3, .label = "downsample-quarter"},
    };
    for (const auto& cls : layout) {
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
    if (!session.renderer_ptr() || !session.renderer_ptr()->framebuffer_pool()) return;

    const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(
        session.canvas_width, session.canvas_height);
    const auto prealloced = session.renderer_ptr()->framebuffer_pool()->preallocate(
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

    warmup_text_size_classes(*session.renderer_ptr()->framebuffer_pool());
}

} // namespace chronon3d::cli
