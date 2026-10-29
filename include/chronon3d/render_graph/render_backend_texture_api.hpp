#pragma once

#include <chronon3d/render_graph/render_backend_types.hpp>
#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <glm/glm.hpp>

#include <span>
#include <cstdint>
#include <optional>

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
    /// Rasterize a projected indexed textured mesh into a native surface.
    /// Each vertex is eight floats (screen x/y, inverse depth, UV x/y, light RGB).
    virtual RenderOpResult draw_inline_mesh_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle texture,
        std::span<const float> projected_lit_vertices, std::span<const std::uint32_t> indices,
        std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
        const Color& premultiplied_tint, std::optional<float> alpha_cutoff);
};

} // namespace chronon3d::graph
