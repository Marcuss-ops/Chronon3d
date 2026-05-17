#pragma once

#include <chronon3d/core/frame.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/scene/scene.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/composition_registry.hpp>
#include <chronon3d/backends/video/video_frame_decoder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <memory>
#include <string>

namespace chronon3d::graph {

/**
 * Executes a scene rendering task using the generic RenderBackend.
 * Centralizes the graph creation, context configuration, execution, and telemetry tracking.
 */
std::shared_ptr<Framebuffer> render_scene_via_graph(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const Scene& scene,
    const Camera& camera,
    i32 width,
    i32 height,
    Frame frame,
    f32 frame_time,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    video::VideoFrameDecoder* video_decoder
);

/**
 * Generates a Graphviz DOT representation of the compiled render graph for diagnostic purposes.
 */
std::string debug_scene_graph(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const Scene& scene,
    const Camera& camera,
    i32 width,
    i32 height,
    Frame frame,
    f32 frame_time,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    video::VideoFrameDecoder* video_decoder
);

/**
 * Renders a single frame of a Composition (handles SSAA, motion blur, telemetry).
 * Replaces software_internal::render_frame — backend-agnostic entry point.
 */
std::unique_ptr<Framebuffer> render_composition_frame(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    video::VideoFrameDecoder* video_decoder,
    const Composition& comp,
    Frame frame
);

// ── Graph diagnostics ──────────────────────────────────────────────────────

struct SceneGraphStats {
    int scene_layers{0};
    std::size_t total_nodes{0};
    GraphNodeId output_id{k_invalid_node};
    std::size_t source_nodes{0};
    std::size_t transform_nodes{0};
    std::size_t effect_nodes{0};
    std::size_t mask_nodes{0};
    std::size_t composite_nodes{0};
    std::size_t adjustment_nodes{0};
    std::size_t precomp_nodes{0};
    std::size_t video_nodes{0};
    std::size_t motion_blur_nodes{0};
    std::size_t other_nodes{0};
    std::size_t cacheable_nodes{0};
    cache::NodeCacheStats cache_before{};
    cache::NodeCacheStats cache_after{};
    double build_ms{0.0};
    double execute_ms{0.0};
    double total_ms{0.0};
    std::string dot;
};

/**
 * Builds the render graph for a scene, counts nodes by kind, optionally executes
 * it for cache/timing diagnostics, and optionally generates the DOT string.
 */
SceneGraphStats analyze_scene_graph(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const Scene& scene,
    const Camera& camera,
    i32 width, i32 height,
    Frame frame, f32 frame_time,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    video::VideoFrameDecoder* video_decoder,
    bool execute = true,
    bool include_dot = false
);

} // namespace chronon3d::graph
