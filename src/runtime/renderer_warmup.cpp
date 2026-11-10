#include <chronon3d/runtime/renderer_warmup.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <stdexcept>
#include <utility>

namespace chronon3d::runtime {

RendererWarmupResult warmup_renderer(
    SoftwareRenderer & renderer,
    const compiler::CompiledProgram& program,
    const RendererWarmupOptions& options) {
    const auto t0 = profiling::now();
    RendererWarmupResult result;
    auto pool = renderer.framebuffer_pool();
    const auto width = options.width > 0 ? options.width : static_cast<int>(program.metadata().width);
    const auto height = options.height > 0 ? options.height : static_cast<int>(program.metadata().height);
    if (options.preallocate_framebuffers && pool) {
        result.framebuffers_created += pool->ensure_preallocated({
            .width = width,
            .height = height,
            .count = std::max<size_t>(4, options.framebuffer_count),
            .clear = true,
            .touch_memory = options.touch_memory});
    }
    for (const auto& res : program.resources()) {
        (void)res;
    }
    for (const auto& layer : program.layers()) {
        (void)layer;
    }
    if (options.render_dummy_frame) {
        for (int pass = 0; pass < 2; ++pass) {
            auto fb = renderer.render_program(program, options.dummy_frame);
            (void)fb;
        }
        // The typed program route is the one the video lane uses: the dummy
        // frames must not be left as the first delivered frame's history.
        renderer.reset_frame_history();
    }
    if (pool) {
        const auto stats = pool->stats();
        result.pool_available_after = stats.available_count;
        result.pool_bytes_after = stats.current_bytes;
    }
    result.elapsed_ms = profiling::duration_ms(t0, profiling::now());
    return result;
}

} // namespace chronon3d::runtime
