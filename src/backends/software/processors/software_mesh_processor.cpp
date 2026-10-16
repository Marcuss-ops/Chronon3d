#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/depth_buffer_pool.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include "../primitive_renderer.hpp"
#include "../specialized/mesh_renderer.hpp"
// R2: draw() now consumes the slim processor context POD.
#include <chronon3d/backends/software/software_processor_context.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace chronon3d::renderer {

class SoftwareMeshProcessor final : public ShapeProcessor {
public:
    void draw(const SoftwareProcessorContext& rctx, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        const auto& mesh_shape = node.shape.mesh_shape();
        if (!mesh_shape.prepared && !mesh_shape.inline_mesh) return;

        Color node_color = node.color.to_linear();
        node_color.a *= state.opacity;
        // Reusable depth buffer from the session pool — no per-frame malloc.
        // Fall back to a local vector for callers without a pool (e.g.
        // legacy tests) so depth testing is never silently disabled.
        std::span<float> depth_buffer;
        std::vector<float> fallback_vec;
        if (rctx.depth_buffer_pool) {
            depth_buffer = rctx.depth_buffer_pool->acquire(
                fb.width(), fb.height());
        } else {
            fallback_vec.resize(
                static_cast<size_t>(fb.width()) * static_cast<size_t>(fb.height()), 0.0f);
            depth_buffer = fallback_vec;
        }

        // Unlit meshes project vertices directly with the 2.5D context, so the
        // scene camera and the mesh share one depth convention. The scene camera
        // comes from the render state; without one, the pass camera seeds it.
        ProjectionContext projection_ctx = state.projection;
        if (!projection_ctx.ready) {
            // Same forward-positive view as the 2.5D scene camera: a layer at z=0
            // in front of the default camera (z=-1000) has positive depth.
            projection_ctx = make_projection_context(camera, width, height);
            projection_ctx.ready = true;
        }

        if (mesh_shape.inline_mesh) {
            Color base = node_color;
            if (node.inline_mesh_material && node.inline_mesh_material->base_color) {
                const Color tint = node.inline_mesh_material->base_color->to_linear();
                base = Color{node_color.r * tint.r, node_color.g * tint.g,
                             node_color.b * tint.b, node_color.a * tint.a};
            }
            if (node.inline_mesh_material && state.light_context.enabled) {
                chronon3d::renderer::render_mesh_filled_lit(
                    fb, *mesh_shape.inline_mesh, state.world_matrix, projection_ctx, base,
                    depth_buffer, state.light_context, state.rim_light,
                    *node.inline_mesh_material, camera.near_plane);
            } else {
                chronon3d::renderer::render_mesh_filled_projected(
                    fb, *mesh_shape.inline_mesh, state.world_matrix, projection_ctx,
                    base, depth_buffer);
            }
        }
        if (mesh_shape.prepared) {
            for (const auto& part : mesh_shape.prepared->parts) {
                if (!part.geometry) continue;
                Color color = node_color;
                float roughness_factor = -1.0f;
                float metallic_factor = 0.0f;
                const assets::MeshImage* metallic_roughness = nullptr;
                RenderNode::InlineMeshMaterial material;
                material.specular = 1.0f;
                if (part.material_index &&
                    *part.material_index < mesh_shape.prepared->materials.size()) {
                    const auto& prepared_material = mesh_shape.prepared->materials[*part.material_index];
                    const Color factor = prepared_material.base_color_factor.to_linear();
                    color = Color{node_color.r * factor.r, node_color.g * factor.g,
                                  node_color.b * factor.b, node_color.a * factor.a};
                    roughness_factor = prepared_material.roughness_factor;
                    metallic_factor = prepared_material.metallic_factor;
                    if (prepared_material.metallic_roughness_texture_index &&
                        *prepared_material.metallic_roughness_texture_index < mesh_shape.prepared->images.size()) {
                        metallic_roughness = &mesh_shape.prepared->images[
                            *prepared_material.metallic_roughness_texture_index];
                    }
                }
                if (state.light_context.enabled) {
                    chronon3d::renderer::render_mesh_filled_lit(
                        fb, *part.geometry, state.world_matrix, projection_ctx, color,
                        depth_buffer, state.light_context, state.rim_light, material,
                        camera.near_plane,
                        roughness_factor, metallic_factor, metallic_roughness);
                } else {
                    chronon3d::renderer::render_mesh_filled_projected(
                        fb, *part.geometry, state.world_matrix, projection_ctx, color,
                        depth_buffer);
                }
            }
        }
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        const auto& mesh_shape = shape.mesh_shape();
        if (!mesh_shape.prepared && !mesh_shape.inline_mesh) return {0, 0, 0, 0};

        raster::BBox result{0, 0, 0, 0};
        bool has_bbox = false;
        const auto merge_bbox = [&](const Mesh& mesh) {
            if (mesh.vertices().empty()) return;
            const auto part_bbox = bbox_for_mesh(mesh, model, spread);
            if (part_bbox.is_empty()) return;
            if (!has_bbox) {
                result = part_bbox;
                has_bbox = true;
            } else {
                result.x0 = std::min(result.x0, part_bbox.x0);
                result.y0 = std::min(result.y0, part_bbox.y0);
                result.x1 = std::max(result.x1, part_bbox.x1);
                result.y1 = std::max(result.y1, part_bbox.y1);
            }
        };
        if (mesh_shape.inline_mesh) merge_bbox(*mesh_shape.inline_mesh);
        if (mesh_shape.prepared)
            for (const auto& part : mesh_shape.prepared->parts)
                if (part.geometry) merge_bbox(*part.geometry);
        return has_bbox ? result : raster::BBox{0, 0, 0, 0};
    }

private:
    static raster::BBox bbox_for_mesh(const Mesh& mesh, const Mat4& model, f32 spread) {
        if (mesh.vertices().empty()) return {0, 0, 0, 0};

        f32 min_x = std::numeric_limits<f32>::max();
        f32 min_y = std::numeric_limits<f32>::max();
        f32 max_x = std::numeric_limits<f32>::lowest();
        f32 max_y = std::numeric_limits<f32>::lowest();
        for (const auto& vertex : mesh.vertices()) {
            const Vec4 p = model * Vec4(vertex.position, 1.0f);
            if (std::abs(p.w) < 1e-6f) continue;
            const f32 x = p.x / p.w;
            const f32 y = p.y / p.w;
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
        if (min_x > max_x || min_y > max_y) return {0, 0, 0, 0};
        return {
            static_cast<i32>(std::floor(min_x - spread)),
            static_cast<i32>(std::floor(min_y - spread)),
            static_cast<i32>(std::ceil(max_x + spread)),
            static_cast<i32>(std::ceil(max_y + spread))};
    }

public:
    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return false;
    }
};

std::unique_ptr<ShapeProcessor> create_mesh_processor() {
    return std::make_unique<SoftwareMeshProcessor>();
}

} // namespace chronon3d::renderer
