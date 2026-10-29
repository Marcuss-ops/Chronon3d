#include "source_node_native.hpp"

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/rendering/lighting_eval.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include "native_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d::graph::detail {

bool try_native_inline_mesh(
    RenderGraphContext& ctx, Framebuffer& fb,
    const ::chronon3d::RenderNode& node, const RenderState& state) {
    if (!ctx.policy.require_native_gpu || !ctx.backend || !ctx.image_cache ||
        !ctx.gpu_asset_cache || !ctx.surface_registry ||
        node.shape.type() != ShapeType::Mesh ||
        !node.shape.mesh_shape().inline_mesh) return false;

    const auto& mesh = *node.shape.mesh_shape().inline_mesh;
    const auto& source_vertices = mesh.vertices();
    const auto& source_indices = mesh.indices();
    if (source_vertices.empty() || source_indices.size() < 3 ||
        source_indices.size() % 3 != 0) return false;

    runtime::RenderSurfaceHandle texture_handle = runtime::kInvalidRenderSurfaceHandle;
    std::shared_ptr<const CachedImage> cached;
    if (!node.inline_mesh_texture_asset.empty()) {
        cached = ctx.image_cache->find(node.inline_mesh_texture_asset);
        if (!cached) cached = ctx.image_cache->get_or_load(node.inline_mesh_texture_asset);
        if (!cached || !cached->valid() || cached->gpu_rgba.empty())
            throw std::runtime_error("inline mesh native texture could not be prepared: " +
                                     node.inline_mesh_texture_asset);
        const auto& key = cached->gpu_key;
        const runtime::SurfaceDesc desc{
            key.width, key.height, key.format, runtime::ResourceUsage::Storage,
            runtime::LifetimeClass::JobPersistent,
            cached->gpu_rgba.size() * sizeof(float)};
        const auto acquired = ctx.gpu_asset_cache->acquire(key, desc, cached->gpu_rgba);
        if (!acquired.ok())
            throw std::runtime_error("inline mesh native texture residency failed: " +
                                     acquired.error);
        texture_handle = acquired.handle;
    } else {
        static const std::array<float, 4> white{1.0f, 1.0f, 1.0f, 1.0f};
        const std::string_view bytes(reinterpret_cast<const char*>(white.data()), sizeof(white));
        const runtime::GpuAssetKey key{
            assets::sha256_string(bytes), runtime::PixelFormat::Rgba32Float, 1, 1};
        const auto format = runtime::make_frame_format(
            runtime::PixelFormat::Rgba32Float, {}, runtime::AlphaMode::Straight);
        const auto desc = runtime::SurfaceDesc::make(
            1, 1, format, runtime::ResourceUsage::Storage,
            runtime::LifetimeClass::JobPersistent);
        const auto acquired = ctx.gpu_asset_cache->acquire(key, desc, white);
        if (!acquired.ok())
            throw std::runtime_error("inline mesh native white texture residency failed: " +
                                     acquired.error);
        texture_handle = acquired.handle;
    }

    if (!ensure_empty_native_surface(ctx, fb)) return false;
    const float aspect = static_cast<float>(fb.width()) / static_cast<float>(fb.height());
    Mat4 view = ctx.frame_input.camera.view_matrix();
    Mat4 projection = ctx.frame_input.camera.projection_matrix(aspect);
    Mat4 mesh_model = state.projection.ready ? state.world_matrix : state.matrix;
    if (state.projection.ready) {
        mesh_model = glm::translate(Mat4(1.0f),
            Vec3(-static_cast<float>(ctx.frame_input.width) * 0.5f,
                 -static_cast<float>(ctx.frame_input.height) * 0.5f, 0.0f)) * mesh_model;
        if (state.projection.focal > 0.0f && fb.height() > 0) {
            Mat4 flip_camera_z{1.0f};
            flip_camera_z[2][2] = -1.0f;
            view = flip_camera_z * state.projection.view;
            const float fov_y = 2.0f * std::atan(
                static_cast<float>(fb.height()) / (2.0f * state.projection.focal));
            projection = glm::perspective(fov_y, aspect,
                ctx.frame_input.camera.near_plane, ctx.frame_input.camera.far_plane);
        }
    }
    const Mat4 view_model = view * mesh_model;
    const float near_plane = std::max(ctx.frame_input.camera.near_plane, 1e-5f);
    const float far_plane = std::max(ctx.frame_input.camera.far_plane, near_plane + 1e-5f);

    Material2_5D lighting_material;
    lighting_material.accepts_lights = true;
    if (node.inline_mesh_material) {
        const auto& source = *node.inline_mesh_material;
        lighting_material.diffuse = source.diffuse;
        lighting_material.specular = source.specular;
        lighting_material.shininess = source.shininess;
        lighting_material.ambient_multiplier = 1.0f;
        lighting_material.metallic = source.metallic;
        lighting_material.roughness = source.roughness;
        lighting_material.double_sided = source.double_sided;
    }
    Color tint = node.color.to_linear();
    tint.a *= state.opacity;
    if (node.inline_mesh_material && node.inline_mesh_material->base_color) {
        const Color base = node.inline_mesh_material->base_color->to_linear();
        tint.r *= base.r;
        tint.g *= base.g;
        tint.b *= base.b;
        tint.a *= base.a;
    }

    Vec3 fallback_normal{0.0f, 0.0f, 1.0f};
    if (source_indices.size() >= 3 && source_indices[0] < source_vertices.size() &&
        source_indices[1] < source_vertices.size() && source_indices[2] < source_vertices.size()) {
        const Vec3 a = source_vertices[source_indices[0]].position;
        const Vec3 b = source_vertices[source_indices[1]].position;
        const Vec3 c = source_vertices[source_indices[2]].position;
        const Vec3 face = glm::cross(b - a, c - a);
        if (glm::dot(face, face) > 1e-8f) fallback_normal = glm::normalize(face);
    }

    // Match the resolved software mesh material lighting. The factor stays
    // per vertex so flat-shaded glTF meshes preserve the CPU oracle exactly,
    // while interpolated vertex normals retain smooth directional lighting.
    std::vector<float> projected(source_vertices.size() * 8, 0.0f);
    std::vector<bool> valid(source_vertices.size(), false);
    float min_x = static_cast<float>(fb.width());
    float min_y = static_cast<float>(fb.height());
    float max_x = 0.0f, max_y = 0.0f;
    for (std::size_t i = 0; i < source_vertices.size(); ++i) {
        const auto& vertex = source_vertices[i];
        const Vec4 view_position = view_model * Vec4(vertex.position, 1.0f);
        const float depth = -view_position.z;
        if (!std::isfinite(depth) || depth < near_plane || depth > far_plane) continue;
        const Vec4 clip = projection * view_position;
        if (!std::isfinite(clip.w) || clip.w <= 1e-6f) continue;
        const float x = (clip.x / clip.w + 1.0f) * 0.5f * static_cast<float>(fb.width());
        const float y = (1.0f - (clip.y / clip.w + 1.0f) * 0.5f) * static_cast<float>(fb.height());
        if (!std::isfinite(x) || !std::isfinite(y)) continue;
        const auto offset = i * 8;
        projected[offset + 0] = x;
        projected[offset + 1] = y;
        projected[offset + 2] = 1.0f / depth;
        projected[offset + 3] = vertex.uv.x;
        projected[offset + 4] = vertex.uv.y;
        const Vec3 input_normal = glm::dot(vertex.normal, vertex.normal) > 1e-8f
            ? vertex.normal : fallback_normal;
        const Vec3 normal = rendering::transform_normal(mesh_model, input_normal);
        const Color lighting = node.inline_mesh_material
            ? rendering::evaluate_lighting(Color{1.0f, 1.0f, 1.0f, 1.0f}, normal,
                                           state.light_context, lighting_material,
                                           &state.rim_light)
            : Color{1.0f, 1.0f, 1.0f, 1.0f};
        projected[offset + 5] = lighting.r;
        projected[offset + 6] = lighting.g;
        projected[offset + 7] = lighting.b;
        valid[i] = true;
        min_x = std::min(min_x, x); min_y = std::min(min_y, y);
        max_x = std::max(max_x, x); max_y = std::max(max_y, y);
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(source_indices.size());
    for (std::size_t i = 0; i < source_indices.size(); i += 3) {
        const auto a = source_indices[i], b = source_indices[i + 1], c = source_indices[i + 2];
        if (a >= valid.size() || b >= valid.size() || c >= valid.size())
            throw std::invalid_argument("inline mesh index is outside the vertex array");
        if (valid[a] && valid[b] && valid[c]) {
            indices.push_back(a); indices.push_back(b); indices.push_back(c);
        }
    }
    if (indices.empty()) return true;

    std::int32_t x0 = std::clamp(static_cast<std::int32_t>(std::floor(min_x)),
                                 0, fb.width());
    std::int32_t y0 = std::clamp(static_cast<std::int32_t>(std::floor(min_y)),
                                 0, fb.height());
    std::int32_t x1 = std::clamp(static_cast<std::int32_t>(std::ceil(max_x)),
                                 0, fb.width());
    std::int32_t y1 = std::clamp(static_cast<std::int32_t>(std::ceil(max_y)),
                                 0, fb.height());
    if (state.clip_rect) {
        x0 = std::max(x0, state.clip_rect->x0 - fb.origin_x());
        y0 = std::max(y0, state.clip_rect->y0 - fb.origin_y());
        x1 = std::min(x1, state.clip_rect->x1 - fb.origin_x());
        y1 = std::min(y1, state.clip_rect->y1 - fb.origin_y());
    }

    auto* texture_api = ctx.backend_texture_api();
    if (!texture_api) return false;
    const auto result = texture_api->draw_inline_mesh_surface(
        fb.surface_handle(), texture_handle, projected, indices,
        x0, y0, x1, y1, tint,
        node.inline_mesh_material ? node.inline_mesh_material->alpha_cutoff
                                 : std::optional<float>{});
    if (!result.ok())
        throw std::runtime_error("native inline mesh raster failed: " + result.error().message);
    fb.set_opaque(false);
    fb.set_content_cleared(true);
    return true;
}

} // namespace chronon3d::graph::detail
