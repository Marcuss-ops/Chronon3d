#pragma once

// Explicit execution dependencies for one software render job.
//
// ADR-029 review usage: this is deliberately a view, not an owner or service
// locator. The
// runtime remains the engine-lifetime owner; runtime_adapter.cpp resolves the
// required domains once and installs this immutable wiring on the renderer.
// RenderPipeline and graph dispatch consume these views instead of retaining
// a RuntimeContext back-pointer.

#include <memory>

namespace chronon3d {

class CompositionRegistry;
class CurveCache;
class ExecutionScheduler;
class FontEngine;
class ImageCache;
class ImageRenderer;
class RenderCounters;
class RenderSettings;
class TextRenderResources;

namespace assets { class AssetResolver; }
namespace cache { class FramebufferPool; class NodeCache; }
namespace effects { class EffectCatalog; }
namespace graph {
class CompiledGraphCache;
class GraphExecutor;
class GraphNodeCatalog;
class RenderBackend;
}
namespace image { class ImageBackend; }
namespace media { class MediaFrameProvider; }
namespace renderer { class SoftwareRegistry; }
namespace runtime {
class RuntimeContext;
class RenderSurfaceRegistry;
class GpuTextRunPreparedCache;
}

/// Registry and policy lookup surface for one software execution job.
struct SoftwareRegistryView {
    renderer::SoftwareRegistry* processors{nullptr};
    graph::GraphNodeCatalog* graph_nodes{nullptr};
    effects::EffectCatalog* effects{nullptr};
    const CompositionRegistry* compositions{nullptr};
};

/// Reusable execution/cache resources borrowed for the job lifetime.
struct SoftwareScratchResources {
    cache::NodeCache* node_cache{nullptr};
    graph::CompiledGraphCache* graph_cache{nullptr};
    std::shared_ptr<cache::FramebufferPool> framebuffer_pool{};
    ExecutionScheduler* scheduler{nullptr};
    CurveCache* curve_cache{nullptr};
};

/// Image and surface services required by graph preparation and dispatch.
struct SoftwareImageServices {
    ImageRenderer* renderer{nullptr};
    image::ImageBackend* backend{nullptr};
    ImageCache* cache{nullptr};
    runtime::RenderSurfaceRegistry* surfaces{nullptr};
};

/// Text/asset services resolved before graph execution begins.
struct SoftwareTextServices {
    TextRenderResources* resources{nullptr};
    FontEngine* fonts{nullptr};
    assets::AssetResolver* assets{nullptr};
    runtime::GpuTextRunPreparedCache* prepared_cache{nullptr};
};

/// Explicit dependency view for a software render job.
///
/// The five named domains are intentionally visible at the type boundary:
/// registry, scratch, image, text and settings. Backend, counters and video
/// are execution inputs rather than additional ownership domains.
struct SoftwareExecutionContext {
    SoftwareRegistryView registry{};
    SoftwareScratchResources scratch{};
    SoftwareImageServices images{};
    SoftwareTextServices text{};
    const RenderSettings* settings{nullptr};

    // Explicit orchestration bridge for legacy graph APIs that still require
    // the engine-lifetime runtime. This is a dependency view, not ownership
    // and not a renderer back-pointer.
    runtime::RuntimeContext* runtime{nullptr};
    graph::GraphExecutor* executor{nullptr};
    graph::RenderBackend* backend{nullptr};
    RenderCounters* counters{nullptr};
    media::MediaFrameProvider* video_decoder{nullptr};
};

} // namespace chronon3d
