#include <chronon3d/runtime/renderer_warmup.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <chrono>

namespace chronon3d::runtime {

RendererWarmupResult warmup_renderer(
    SoftwareRenderer& renderer,
    const Composition& composition,
    const RendererWarmupOptions& options)
{
    const auto t0 = std::chrono::steady_clock::now();

    RendererWarmupResult result;

    auto pool = renderer.framebuffer_pool();

    // 1. Preallocate framebuffers into the pool
    if (options.preallocate_framebuffers && pool) {
        // Primary canvas size: keep the hot path focused on the actual render target.
        result.framebuffers_created += pool->preallocate({
            .width = options.width,
            .height = options.height,
            .count = std::max<size_t>(2, options.framebuffer_count),
            .clear = true,
            .touch_memory = options.touch_memory
        });
    }

    // 2. Optionally render a dummy frame to prime all caches.
    // Run it twice so the second pass exercises the fully warmed pool and cache.
    if (options.render_dummy_frame) {
        for (int pass = 0; pass < 2; ++pass) {
            auto scene = composition.evaluate(options.dummy_frame);
            auto fb = renderer.render_scene(
                scene,
                composition.camera,
                composition.width(),
                composition.height());
            (void)fb; // discard the result
        }
    }

    // 3. Capture post-warmup pool stats
    if (pool) {
        auto stats = pool->stats();
        result.pool_available_after = stats.available_count;
        result.pool_bytes_after = stats.current_bytes;
    }

    const auto t1 = std::chrono::steady_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    return result;
}

} // namespace chronon3d::runtime
