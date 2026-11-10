#pragma once

#include <chronon3d/render_graph/render_backend_types.hpp>
#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <glm/glm.hpp>

#include <span>
#include <cstdint>
#include <optional>

namespace chronon3d::graph {

/// Per-pixel material payload for the inline-mesh rasterizer.
///
/// When `metallic_roughness_texture` is valid and `per_pixel_pbr` is set, the
/// backend samples G (roughness) and B (metallic) per pixel, derives the
/// Blinn-Phong shininess and the metallic energy split, and shades the
/// interpolated world-space normal with one directional light plus ambient —
/// the same contract as the software `render_mesh_filled_lit` path. The
/// minimum guaranteed push-constant budget (128 bytes) is fully consumed, so
/// the environment probe and explicit rim-light overrides stay fail-closed on
/// this lane instead of being ignored.
struct InlineMeshShadingParams {
    runtime::RenderSurfaceHandle metallic_roughness_texture{
        runtime::kInvalidRenderSurfaceHandle};
    bool per_pixel_pbr{false};
    bool double_sided{false};
    bool directional_enabled{false};
    bool ambient_enabled{false};
    bool emissive_enabled{false};
    float metallic{0.0f};
    float roughness{1.0f};
    float diffuse{1.0f};
    float light_diffuse{0.0f};
    float ambient{0.0f};
    glm::vec3 light_direction{0.0f, 0.0f, 1.0f};
    glm::vec3 light_color{1.0f, 1.0f, 1.0f};
    glm::vec3 ambient_color{1.0f, 1.0f, 1.0f};
    glm::vec3 emissive{0.0f, 0.0f, 0.0f};
};

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
        std::span<const runtime::RenderSurfaceHandle> atlas_pages,
        std::span<const runtime::GlyphPaint> paints);
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
    /// Each vertex is eleven floats (screen x/y, inverse depth, UV x/y, the
    /// host-evaluated light RGB, then the world-space normal xyz used by the
    /// per-pixel material path).
    /// @param depth_destination Optional RGBA32F companion surface that
    ///        receives r = per-pixel camera depth, g = `depth_marker` (1 =
    ///        coverage) for every covered pixel. This is how a GPU-only mesh
    ///        carries per-pixel depth into native compositing, where no CPU
    ///        depth map exists. Pass kInvalidRenderSurfaceHandle to skip it.
    virtual RenderOpResult draw_inline_mesh_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle texture,
        std::span<const float> projected_lit_vertices, std::span<const std::uint32_t> indices,
        std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
        const Color& premultiplied_tint, std::optional<float> alpha_cutoff,
        runtime::RenderSurfaceHandle depth_destination =
            runtime::kInvalidRenderSurfaceHandle,
        float depth_marker = 1.0f,
        const InlineMeshShadingParams& shading = {});
};

} // namespace chronon3d::graph
