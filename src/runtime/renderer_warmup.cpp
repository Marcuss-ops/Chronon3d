#include <chronon3d/runtime/renderer_warmup.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/effects/effect_params.hpp>

#include <chrono>
#include <limits>

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
            .count = std::max<size_t>(4, options.framebuffer_count),
            .clear = true,
            .touch_memory = options.touch_memory
        });

        // Intermediate size (typical image layer ~800×450 → bucket 896×512).
        // Pre-touch these pages so FocusInLadder blur levels and composite
        // intermediates don't incur minor page faults during the render loop.
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

    // 2b. Prime FocusInLadder blur-ladder cache by rendering frames where
    //     the effect is active.  Uses render_frame() (same path as the
    //     render loop) to guarantee identical graph construction, clip
    //     rects, and cache dimensions.  Renders both frame 0 and frame 1
    //     to cover the animation range — the render loop typically starts
    //     at frame 0, and the 32-aligned bucketing may not absorb the
    //     dimension change between frames 0→1.
    //
    //     CRITICAL: the preceding render_dummy_frame block (via
    //     render_scene → render_scene_via_graph) may cache compiled
    //     graph state and scene fingerprints that differ from what
    //     render_frame → render_composition_frame produces (e.g. SSAA
    //     wrapping, composition evaluation vs scene path). Reusing
    //     that cached state causes different intermediate framebuffer
    //     dimensions → different FocusInLadder cache key → precompute
    //     fires again during the render loop.
    //
    //     We reset all prev-state so the warmup renders build fresh
    //     graphs via the render_frame path, matching the render loop.
    //     Works independently of render_dummy_frame.
    if (options.warmup_focus_in_ladder) {
        // Invalidate all cached state that could cause graph reuse
        // or fast-path triggers.  The render loop's frame 0 also
        // starts fresh (different frame from warmup frame 1 → no
        // fingerprint match → fresh graph build).
        renderer.graph_cache().reset();
        renderer.m_prev_graph_structure_fingerprint = 0;
        renderer.m_prev_static_scene_fingerprint = 0;
        renderer.m_prev_scene_fingerprint = 0;
        renderer.m_prev_active_at_fingerprint = 0;
        renderer.m_prev_frame = std::numeric_limits<Frame>::max();

        // Render frames consecutively (step=1) up to max_warmup_frame
        // to guarantee identical dirty-rect state and clip rects as the
        // render loop.  Frame 0 starts with dirty rects disabled; all
        // subsequent frames are consecutive → dirty rects enabled → the
        // same 64-aligned buckets the render loop will encounter.
        //
        // Step-interval rendering (e.g., every 5th frame) breaks this:
        // non-consecutive m_prev_frame disables dirty rects, producing
        // full predicted_bbox clips that differ from the render loop's
        // dirty-rect-intersected clips → different cache keys → cache
        // misses during render.
        const Frame max_frame = options.focus_in_ladder_max_warmup_frame > 0
            ? std::min(options.focus_in_ladder_max_warmup_frame, composition.duration())
            : composition.duration();
        for (Frame f = 0; f <= max_frame; ++f) {
            auto fb = renderer.render_frame(composition, f);
            (void)fb;
        }

        // Capture the precompute time before counters are reset by the
        // caller.  This value is surfaced in the telemetry dashboard as
        // effect_focus_in_ladder_warmup_ms.
        if (auto* cnt = renderer.counters()) {
            result.focus_in_ladder_precompute_ms = static_cast<double>(
                cnt->effect_focus_in_ladder_precompute_ms.load(std::memory_order_relaxed));
        }

        // Prune the ladder cache to reclaim memory before the render loop.
        // All dimension buckets were precomputed during warmup; we keep only
        // the smallest entries (earliest animation frames) and evict the rest.
        // Evicted entries will be re-precomputed on demand during the render
        // loop if needed, at a one-time cost per bucket (typically <100ms).
        ::chronon3d::renderer::prune_focus_in_ladder_cache();
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
