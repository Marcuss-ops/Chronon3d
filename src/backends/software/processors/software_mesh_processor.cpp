#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
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
        const auto source = mesh_shape.prepared;
        const auto legacy_mesh = mesh_shape.mesh;
        if (!source && !legacy_mesh) return;

        Color node_color = node.color.to_linear();
        node_color.a *= state.opacity;
        const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
        const Mat4 view = camera.view_matrix();
        const Mat4 projection = camera.projection_matrix(aspect);
        std::vector<float> depth_buffer(
            static_cast<size_t>(fb.width()) * static_cast<size_t>(fb.height()), 0.0f);

        if (source) {
            for (const auto& part : source->parts) {
                if (!part.geometry) continue;
                Color color = node_color;
                if (part.material_index &&
                    *part.material_index < source->materials.size()) {
                    const Color material =
                        source->materials[*part.material_index].base_color_factor.to_linear();
                    color = Color{
                        node_color.r * material.r,
                        node_color.g * material.g,
                        node_color.b * material.b,
                        node_color.a * material.a};
                }
                chronon3d::renderer::render_mesh_filled(
                    fb, *part.geometry, state.matrix, view, projection, color,
                    depth_buffer, camera.near_plane, camera.far_plane);
            }
            return;
        }

        // Compatibility for callers that provide an in-memory Mesh directly.
        chronon3d::renderer::render_mesh_filled(
            fb, *legacy_mesh, state.matrix, view, projection, node_color,
            depth_buffer, camera.near_plane, camera.far_plane);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        const auto& mesh_shape = shape.mesh_shape();
        if (mesh_shape.mesh) {
            return bbox_for_mesh(*mesh_shape.mesh, model, spread);
        }
        if (!mesh_shape.prepared) return {0, 0, 0, 0};

        raster::BBox result{0, 0, 0, 0};
        bool has_bbox = false;
        for (const auto& part : mesh_shape.prepared->parts) {
            if (!part.geometry || part.geometry->vertices().empty()) continue;
            const auto part_bbox = bbox_for_mesh(*part.geometry, model, spread);
            if (part_bbox.is_empty()) continue;
            if (!has_bbox) {
                result = part_bbox;
                has_bbox = true;
            } else {
                result.x0 = std::min(result.x0, part_bbox.x0);
                result.y0 = std::min(result.y0, part_bbox.y0);
                result.x1 = std::max(result.x1, part_bbox.x1);
                result.y1 = std::max(result.y1, part_bbox.y1);
            }
        }
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
