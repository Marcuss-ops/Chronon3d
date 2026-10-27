#pragma once

#include <chronon3d/render_graph/render_backend_types.hpp>
#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <glm/glm.hpp>

#include <span>

namespace chronon3d::graph {

/// Textures, glyph atlases, and compiled layer-batch drawing operations.
class RenderBackendTextureApi {
public:
    virtual ~RenderBackendTextureApi() = default;

    virtual RenderOpResult draw_text_run(
        Framebuffer& framebuffer, const chronon3d::TextRunShape& shape,
        const glm::mat4& model_matrix, float opacity);
    virtual RenderOpResult draw_text_run_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle atlas,
        std::span<const runtime::GlyphInstance> glyphs);
    virtual RenderOpResult draw_text_run_surface_timed(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle atlas,
        std::span<const runtime::GlyphInstance> glyphs, float current_frame,
        const Color& highlight_color, bool highlight_enabled);
    virtual RenderOpResult draw_text_batch(
        runtime::RenderSurfaceHandle destination,
        std::span<const runtime::GlyphStatic> glyphs,
        std::span<const runtime::TextRunDynamic> runs,
        std::span<const runtime::RenderSurfaceHandle> atlas_pages);
    virtual RenderOpResult execute_layer_batch(
        runtime::RenderSurfaceHandle destination,
        std::span<const runtime::LayerInstance> instances,
        std::span<const runtime::RenderSurfaceHandle> resources,
        std::span<const float> transforms, std::span<const float> paints);
    virtual RenderOpResult execute_layer_batch(
        runtime::RenderSurfaceHandle destination, const runtime::GpuLayerBatch& batch,
        std::span<const runtime::RenderSurfaceHandle> resources,
        std::span<const float> transforms, std::span<const float> paints);
};

} // namespace chronon3d::graph
