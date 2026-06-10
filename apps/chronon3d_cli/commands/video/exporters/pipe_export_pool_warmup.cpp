#include "../common/pipe_export_session.hpp"

#include <chronon3d/cache/framebuffer_pool.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

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
            .count = 6,
            .clear = true,
            .touch_memory = false,
        });
    if (prealloced > 0) {
        spdlog::info("[pool-warm] Pre-allocated {} canvas buffers ({}x{} bucket) at startup",
                     prealloced, bw, bh);
    }
}

} // namespace chronon3d::cli
