#pragma once

#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/frame.hpp>

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

    size_t framebuffer_count{16};
    bool preallocate_framebuffers{true};
    bool touch_memory{true};

    bool render_dummy_frame{false};
    Frame dummy_frame{0};

    bool quiet{false};
};

// ---------------------------------------------------------------------------
// RendererWarmupResult
// ---------------------------------------------------------------------------
struct RendererWarmupResult {
    size_t framebuffers_created{0};
    size_t pool_available_after{0};
    size_t pool_bytes_after{0};
    double elapsed_ms{0.0};
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
