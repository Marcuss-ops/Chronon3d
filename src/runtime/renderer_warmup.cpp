#include <chronon3d/runtime/renderer_warmup.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <stdexcept>
#include <utility>

namespace chronon3d::runtime {

RendererWarmupResult warmup_renderer(
    SoftwareRenderer & renderer,
    const Composition& composition,
    const RendererWarmupOptions& options)
{
    const auto t0 = profiling::now();

    RendererWarmupResult result;

    auto pool = renderer.framebuffer_pool();

    // 1. Preallocate framebuffers into the pool
    if (options.preallocate_framebuffers && pool) {
        // Primary canvas size: keep the hot path focused on the actual render target.
        result.framebuffers_created += pool->preallocate({
            .width = options.width,
            .height = options.height,
            .count = std::max<size_t>(4, options.framebuffer_count),
            .clear = true,
            .touch_memory = options.touch_memory
        });

        // Intermediate size (typical image layer ~800×450 → bucket 896×512).
        // Pre-touch these pages so composite intermediates don't incur minor
        // page faults during the render loop.
        result.framebuffers_created += pool->preallocate({
            .width = 896,
            .height = 512,
            .count = std::max<size_t>(4, options.framebuffer_count / 2),
            .clear = true,
            .touch_memory = options.touch_memory
        });

        // Medium intermediate size for effect scratch / compositing.
        result.framebuffers_created += pool->preallocate({
            .width = 1024,
            .height = 1024,
            .count = std::max<size_t>(2, options.framebuffer_count / 4),
            .clear = true,
            .touch_memory = options.touch_memory
        });
    }

    // 2. Optionally render dummy frame(s) to prime all caches.
    // Run it twice so the second pass exercises the fully warmed pool and cache.
    if (options.render_dummy_frame) {
        auto compiled = chronon3d::compile_composition(
            composition, CompositionCompileContext{});
        if (!compiled) {
            throw std::runtime_error(
                "Composition compilation failed during warmup: " +
                compiled.error().message);
        }
        auto compiled_value = std::move(compiled).value();
        for (int pass = 0; pass < 2; ++pass) {
            auto fb = renderer.render_compiled(compiled_value, options.dummy_frame);
            (void)fb; // discard the result
        }
    }

    // 3. Capture post-warmup pool stats
    if (pool) {
        auto stats = pool->stats();
        result.pool_available_after = stats.available_count;
        result.pool_bytes_after = stats.current_bytes;
    }

    result.elapsed_ms = profiling::elapsed_ms(t0);

    return result;
}

RendererWarmupResult warmup_renderer(
    SoftwareRenderer & renderer,
    const CompiledComposition& compiled,
    const RendererWarmupOptions& options) {
    const auto t0 = profiling::now();
    RendererWarmupResult result;
    auto pool = renderer.framebuffer_pool();
    if (options.preallocate_framebuffers && pool) {
        result.framebuffers_created += pool->preallocate({
            .width = options.width,
            .height = options.height,
            .count = std::max<size_t>(4, options.framebuffer_count),
            .clear = true,
            .touch_memory = options.touch_memory});
    }
    if (options.render_dummy_frame) {
        for (int pass = 0; pass < 2; ++pass) {
            auto fb = renderer.render_compiled(compiled, options.dummy_frame);
            (void)fb;
        }
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
