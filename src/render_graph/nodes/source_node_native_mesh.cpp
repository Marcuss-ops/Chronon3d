#include "source_node_native.hpp"

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/backends/image/image_decode_options.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/rendering/lighting_eval.hpp>
#include "detail/native_promotion.hpp"
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include "native_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
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
    if (!ctx.backend || !ctx.backend_stats_api()->supports_native_surfaces() ||
        !ctx.image_cache || !ctx.gpu_asset_cache || !ctx.surface_registry) return false;

    // A projected RenderPlan color rectangle is the same flat quad geometry
    // consumed by the inline-mesh native rasterizer. Promote only the exact
    // solid, unrounded, unmasked rectangle contract; other projected shapes
    // keep their existing fail-closed behavior.
    std::optional<::chronon3d::RenderNode> projected_rect_node;
    const ::chronon3d::RenderNode* draw_node = &node;
    if (node.shape.type() == ShapeType::Rect && state.projection.ready &&
        native_promotion::is_projected(state) &&
        has_native_solid_rect_fill(node) &&
        !node.shape.rect().stroke.enabled && node.corner_radius <= 0.0f &&
        !native_promotion::has_active_mask(state)) {
        const Vec2 size = node.shape.rect().size;
        if (!std::isfinite(size.x) || !std::isfinite(size.y) ||
            size.x <= 0.0f || size.y <= 0.0f) return false;
        auto quad = std::make_shared<Mesh>("projected-color-rect");
        const Vec3 normal{0.0f, 0.0f, 1.0f};
        quad->add_vertex(Vertex{{0.0f, 0.0f, 0.0f}, normal, {0.0f, 0.0f}});
        quad->add_vertex(Vertex{{size.x, 0.0f, 0.0f}, normal, {1.0f, 0.0f}});
        quad->add_vertex(Vertex{{size.x, size.y, 0.0f}, normal, {1.0f, 1.0f}});
        quad->add_vertex(Vertex{{0.0f, size.y, 0.0f}, normal, {0.0f, 1.0f}});
        quad->add_index(0); quad->add_index(1); quad->add_index(2);
        quad->add_index(0); quad->add_index(2); quad->add_index(3);
        projected_rect_node.emplace(node);
        projected_rect_node->shape.set_type(ShapeType::Mesh);
        projected_rect_node->shape.mesh_shape().inline_mesh = std::move(quad);
        draw_node = &*projected_rect_node;
    }

    if (draw_node->shape.type() != ShapeType::Mesh ||
        !draw_node->shape.mesh_shape().inline_mesh) return false;

    const auto& mesh = *draw_node->shape.mesh_shape().inline_mesh;
    const auto& source_vertices = mesh.vertices();
    const auto& source_indices = mesh.indices();
    if (source_vertices.empty() || source_indices.size() < 3 ||
        source_indices.size() % 3 != 0) return false;

    runtime::RenderSurfaceHandle texture_handle = runtime::kInvalidRenderSurfaceHandle;
    std::shared_ptr<const CachedImage> cached;
    if (!draw_node->inline_mesh_texture_asset.empty()) {
        cached = ctx.image_cache->find(draw_node->inline_mesh_texture_asset);
        if (!cached) cached = ctx.image_cache->get_or_load(draw_node->inline_mesh_texture_asset);
        if (!cached || !cached->valid() || cached->gpu_rgba.empty())
            throw std::runtime_error("inline mesh native texture could not be prepared: " +
                                     draw_node->inline_mesh_texture_asset);
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

    runtime::RenderSurfaceHandle mr_texture_handle =
        runtime::kInvalidRenderSurfaceHandle;
    if (!draw_node->inline_mesh_metallic_roughness_texture_asset.empty()) {
        ImageDecodeOptions mr_options;
        mr_options.color_space = ColorSpace::LinearSRGB;
        auto mr_cached = ctx.image_cache->find(
            draw_node->inline_mesh_metallic_roughness_texture_asset, mr_options);
        if (!mr_cached)
            mr_cached = ctx.image_cache->get_or_load(
                draw_node->inline_mesh_metallic_roughness_texture_asset, mr_options);
        if (!mr_cached || !mr_cached->valid() || mr_cached->gpu_rgba.empty())
            throw std::runtime_error(
                "inline mesh native metallic-roughness texture could not be prepared: " +
                draw_node->inline_mesh_metallic_roughness_texture_asset);
        const auto& key = mr_cached->gpu_key;
        const runtime::SurfaceDesc desc{
            key.width, key.height, key.format, runtime::ResourceUsage::Storage,
            runtime::LifetimeClass::JobPersistent,
            mr_cached->gpu_rgba.size() * sizeof(float)};
        const auto acquired = ctx.gpu_asset_cache->acquire(key, desc, mr_cached->gpu_rgba);
        if (!acquired.ok())
            throw std::runtime_error(
                "inline mesh native metallic-roughness residency failed: " + acquired.error);
        mr_texture_handle = acquired.handle;
    }

    if (!ensure_empty_native_surface(ctx, fb)) return false;

    // Per-pixel camera depth for native compositing. A GPU-only layer has no
    // CPU depth map by contract, so the rasterizer publishes depth into the
    // frame's occlusion scratch: reset to the "no coverage" sentinel first,
    // then let the kernel keep the nearest camera depth of each covered
    // pixel. The handle travels with the framebuffer and CompositeNode reads
    // it to occlude against geometry already composited this frame.
    runtime::RenderSurfaceHandle depth_stamp = runtime::kInvalidRenderSurfaceHandle;
    if (ctx.frame_input.has_camera_2_5d && ctx.frame_input.camera_2_5d.enabled &&
        fb.width() == ctx.frame_input.width &&
        fb.height() == ctx.frame_input.height) {
        auto& occlusion = ctx.node_exec.native_occlusion_state();
        if (!occlusion.unusable) {
            std::shared_ptr<Framebuffer>* depth_owner = &occlusion.layer_depth;
            if (ctx.node_exec.current_identity.valid()) {
                depth_owner = &occlusion.source_layer_depth[ctx.node_exec.current_identity];
            }
            if (!*depth_owner ||
                (*depth_owner)->width() != fb.width() ||
                (*depth_owner)->height() != fb.height() ||
                (*depth_owner)->surface_handle() ==
                    runtime::kInvalidRenderSurfaceHandle) {
                if (*depth_owner)
                    release_native_surface(ctx, **depth_owner);
                *depth_owner = std::make_shared<Framebuffer>(fb.width(), fb.height());
                if (!ensure_empty_native_surface(ctx, **depth_owner)) {
                    depth_owner->reset();
                    occlusion.unusable = true;
                }
            }
            if (*depth_owner) {
                const auto reset = ctx.backend_surface_api()->dof_depth_stamp_surface(
                    (*depth_owner)->surface_handle(), fb.surface_handle(),
                    0.0f, -1.0f, 0, 0, 0, 0, 0, 0, true);
                if (reset.ok()) depth_stamp = (*depth_owner)->surface_handle();
                else occlusion.unusable = true;
            }
        }
    }

    const float aspect = static_cast<float>(fb.width()) / static_cast<float>(fb.height());
    const float near_plane = std::max(
        ctx.frame_input.projection_ctx.ready
            ? ctx.frame_input.projection_ctx.near_plane
            : ctx.frame_input.camera.near_plane,
        1e-5f);
    const float far_plane = std::max(
        ctx.frame_input.projection_ctx.ready
            ? ctx.frame_input.projection_ctx.far_plane
            : ctx.frame_input.camera.far_plane,
        near_plane + 1e-5f);
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
            projection = glm::perspective(fov_y, aspect, near_plane, far_plane);
        }
    }
    const Mat4 view_model = view * mesh_model;

    Material2_5D lighting_material;
    lighting_material.accepts_lights = true;
    if (draw_node->inline_mesh_material) {
        const auto& source = *draw_node->inline_mesh_material;
        lighting_material.diffuse = source.diffuse;
        lighting_material.specular = source.specular;
        lighting_material.shininess = source.shininess;
        lighting_material.ambient_multiplier = 1.0f;
        lighting_material.metallic = source.metallic;
        lighting_material.roughness = source.roughness;
        lighting_material.double_sided = source.double_sided;
    }
    Color tint = draw_node->color.to_linear();
    tint.a *= state.opacity;
    if (draw_node->inline_mesh_material && draw_node->inline_mesh_material->base_color) {
        const Color base = draw_node->inline_mesh_material->base_color->to_linear();
        tint.r *= base.r;
        tint.g *= base.g;
        tint.b *= base.b;
        tint.a *= base.a;
    }

    // Per-pixel material path. It mirrors render_mesh_filled_lit exactly for
    // the directional+ambient+rim model; feature combinations the bounded
    // Vibranium push-constant block cannot represent fail closed rather than
    // silently rendering a different image.
    graph::InlineMeshShadingParams shading;
    if (draw_node->inline_mesh_material) {
        const auto& source = *draw_node->inline_mesh_material;
        shading.double_sided = source.double_sided;
        if (source.emissive) {
            const Color emissive = source.emissive->to_linear();
            shading.emissive_enabled = true;
            shading.emissive = {emissive.r, emissive.g, emissive.b};
        }
        if (source.roughness && state.light_context.enabled) {
            if (state.light_context.environment_enabled ||
                state.light_context.environment_probe)
                throw std::runtime_error(
                    "Vulkan inline mesh per-pixel PBR does not support HDRI environment lighting");
            if (state.rim_light.enabled)
                throw std::runtime_error(
                    "Vulkan inline mesh per-pixel PBR does not support an explicit rim light");
            shading.per_pixel_pbr = mr_texture_handle !=
                runtime::kInvalidRenderSurfaceHandle;
            shading.metallic_roughness_texture = mr_texture_handle;
            shading.metallic = source.metallic.value_or(0.0f);
            shading.roughness = *source.roughness;
            shading.diffuse = source.diffuse;
            shading.directional_enabled = state.light_context.directional_enabled;
            shading.ambient_enabled = state.light_context.ambient_enabled;
            shading.light_direction = state.light_context.direction;
            shading.light_color = {state.light_context.directional_color.r,
                                   state.light_context.directional_color.g,
                                   state.light_context.directional_color.b};
            shading.ambient_color = {state.light_context.ambient_color.r,
                                     state.light_context.ambient_color.g,
                                     state.light_context.ambient_color.b};
            shading.light_diffuse = state.light_context.diffuse;
            shading.ambient = state.light_context.ambient;
        }
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

    // Clip in view space before dividing by w. UV and per-vertex lighting
    // must follow the new edge vertices, otherwise a triangle crossing a
    // near/far plane disappears or acquires discontinuous shading.
    struct ClipVertex {
        Vec4 view_position;
        Vec2 uv;
        Vec3 light;
        Vec3 normal;
    };
    std::vector<ClipVertex> vertices;
    vertices.reserve(source_vertices.size());
    for (const auto& vertex : source_vertices) {
        const Vec3 input_normal = glm::dot(vertex.normal, vertex.normal) > 1e-8f
            ? vertex.normal : fallback_normal;
        const Vec3 normal = rendering::transform_normal(mesh_model, input_normal);
        const Color lighting = draw_node->inline_mesh_material
            ? rendering::evaluate_lighting(Color{1.0f, 1.0f, 1.0f, 1.0f}, normal,
                                           state.light_context, lighting_material,
                                           &state.rim_light)
            : Color{1.0f, 1.0f, 1.0f, 1.0f};
        vertices.push_back({view_model * Vec4(vertex.position, 1.0f), vertex.uv,
                            Vec3{lighting.r, lighting.g, lighting.b}, normal});
    }
    std::vector<float> projected(source_vertices.size() * 11, 0.0f);
    std::vector<bool> valid(source_vertices.size(), false);
    float min_x = static_cast<float>(fb.width());
    float min_y = static_cast<float>(fb.height());
    float max_x = 0.0f, max_y = 0.0f;
    const auto project = [&](const ClipVertex& vertex, std::size_t index) {
        const float depth = -vertex.view_position.z;
        if (!std::isfinite(depth) || depth <= 0.f) return false;
        const Vec4 clip = projection * vertex.view_position;
        if (!std::isfinite(clip.w) || clip.w <= 1e-6f) return false;
        const float x = (clip.x / clip.w + 1.0f) * 0.5f * static_cast<float>(fb.width());
        const float y = (1.0f - (clip.y / clip.w + 1.0f) * 0.5f) * static_cast<float>(fb.height());
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        const auto offset = index * 11;
        projected[offset] = x;
        projected[offset + 1] = y;
        projected[offset + 2] = 1.0f / depth;
        projected[offset + 3] = vertex.uv.x;
        projected[offset + 4] = vertex.uv.y;
        projected[offset + 5] = vertex.light.x;
        projected[offset + 6] = vertex.light.y;
        projected[offset + 7] = vertex.light.z;
        projected[offset + 8] = vertex.normal.x;
        projected[offset + 9] = vertex.normal.y;
        projected[offset + 10] = vertex.normal.z;
        min_x = std::min(min_x, x); min_y = std::min(min_y, y);
        max_x = std::max(max_x, x); max_y = std::max(max_y, y);
        return true;
    };
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const float depth = -vertices[i].view_position.z;
        if (depth >= near_plane && depth <= far_plane) valid[i] = project(vertices[i], i);
    }
    std::vector<std::uint32_t> indices;
    indices.reserve(source_indices.size());
    for (std::size_t i = 0; i < source_indices.size(); i += 3) {
        const auto a = source_indices[i], b = source_indices[i + 1], c = source_indices[i + 2];
        if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size())
            throw std::invalid_argument("inline mesh index is outside the vertex array");
        if (valid[a] && valid[b] && valid[c]) {
            indices.insert(indices.end(), {a, b, c});
            continue;
        }
        std::vector<ClipVertex> polygon{vertices[a], vertices[b], vertices[c]};
        const auto clip_depth = [&](float plane, bool is_near) {
            std::vector<ClipVertex> clipped;
            if (polygon.empty()) return clipped;
            for (std::size_t j = 0; j < polygon.size(); ++j) {
                const auto& v0 = polygon[j];
                const auto& v1 = polygon[(j + 1) % polygon.size()];
                const float d0 = -v0.view_position.z, d1 = -v1.view_position.z;
                if (!std::isfinite(d0) || !std::isfinite(d1)) return std::vector<ClipVertex>{};
                const bool in0 = is_near ? d0 >= plane : d0 <= plane;
                const bool in1 = is_near ? d1 >= plane : d1 <= plane;
                if (in0 != in1) {
                    const float t = (plane - d0) / (d1 - d0);
                    clipped.push_back({glm::mix(v0.view_position, v1.view_position, t),
                                       glm::mix(v0.uv, v1.uv, t),
                                       glm::mix(v0.light, v1.light, t),
                                       glm::mix(v0.normal, v1.normal, t)});
                }
                if (in1) clipped.push_back(v1);
            }
            return clipped;
        };
        polygon = clip_depth(near_plane, true);
        polygon = clip_depth(far_plane, false);
        if (polygon.size() < 3) continue;
        if (projected.size() / 11 > std::numeric_limits<std::uint32_t>::max() - polygon.size())
            throw std::length_error("clipped inline mesh exceeds index limits");
        const auto first = static_cast<std::uint32_t>(projected.size() / 11);
        projected.resize(projected.size() + polygon.size() * 11);
        bool all_projected = true;
        for (std::size_t j = 0; j < polygon.size(); ++j)
            all_projected = project(polygon[j], first + j) && all_projected;
        if (!all_projected) continue;
        for (std::uint32_t j = 1; j + 1 < polygon.size(); ++j)
            indices.insert(indices.end(), {first, first + j, first + j + 1});
    }
    if (indices.empty()) return true;

    const auto x0f = std::clamp(std::floor(min_x), 0.f, static_cast<float>(fb.width()));
    const auto y0f = std::clamp(std::floor(min_y), 0.f, static_cast<float>(fb.height()));
    const auto x1f = std::clamp(std::ceil(max_x), 0.f, static_cast<float>(fb.width()));
    const auto y1f = std::clamp(std::ceil(max_y), 0.f, static_cast<float>(fb.height()));
    std::int32_t x0 = static_cast<std::int32_t>(x0f);
    std::int32_t y0 = static_cast<std::int32_t>(y0f);
    std::int32_t x1 = static_cast<std::int32_t>(x1f);
    std::int32_t y1 = static_cast<std::int32_t>(y1f);
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
        draw_node->inline_mesh_material ? draw_node->inline_mesh_material->alpha_cutoff
                                 : std::optional<float>{},
        depth_stamp, 1.0f, shading);
    if (!result.ok())
        throw std::runtime_error("native inline mesh raster failed: " + result.error().message);
    if (depth_stamp != runtime::kInvalidRenderSurfaceHandle)
        fb.set_native_depth_handle(depth_stamp);
    fb.set_opaque(false);
    fb.set_content_cleared(true);
    return true;
}

} // namespace chronon3d::graph::detail
