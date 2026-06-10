#pragma once

#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/types/frame.hpp>

#include <memory>
#include <string>

namespace chronon3d {
class SoftwareRenderer;
}

namespace chronon3d::runtime {

// ---------------------------------------------------------------------------
// RendererWarmupOptions
// ---------------------------------------------------------------------------
struct RendererWarmupOptions {
    int width{1920};
    int height{1080};

    size_t framebuffer_count{8};
    bool preallocate_framebuffers{true};
    bool touch_memory{true};

    bool render_dummy_frame{false};
    Frame dummy_frame{0};

    bool quiet{false};

    /// When true, renders frames consecutively (step=1) during warmup to
    /// prime the FocusInLadder blur-ladder cache.  Consecutive rendering
    /// guarantees identical dirty-rect state and clip rects as the render
    /// loop, so all 64-aligned dimension buckets are precomputed.
    bool warmup_focus_in_ladder{true};
    /// Maximum frame (inclusive) to warm up when warmup_focus_in_ladder
    /// is true.  Frames 0..max_warmup_frame are rendered consecutively.
    /// Default 60 covers most FocusInLadder animations (typical duration
    /// 30-90 frames).  Set to 0 to use composition.duration().
    Frame focus_in_ladder_max_warmup_frame{60};
};

// ---------------------------------------------------------------------------
// RendererWarmupResult
// ---------------------------------------------------------------------------
struct RendererWarmupResult {
    size_t framebuffers_created{0};
    size_t pool_available_after{0};
    size_t pool_bytes_after{0};
    double elapsed_ms{0.0};
    /// FocusInLadder precompute wall-time captured during warmup (ms).
    /// Zero if FocusInLadder was not triggered during warmup.
    double focus_in_ladder_precompute_ms{0.0};
};

// ---------------------------------------------------------------------------
// warmup_renderer
// ---------------------------------------------------------------------------
/// Pre-warm the renderer by preallocating framebuffers into the pool and
/// optionally rendering a dummy frame to prime caches and resolve page faults
/// before the actual render session begins.
RendererWarmupResult warmup_renderer(
    ::chronon3d::SoftwareRenderer& renderer,
    const Composition& composition,
    const RendererWarmupOptions& options);

} // namespace chronon3d::runtime
