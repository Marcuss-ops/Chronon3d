#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/assets/image_renderer.hpp>
#include <chronon3d/assets/mesh_loader.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/depth_buffer_pool.hpp>
#include "../primitive_renderer.hpp"
#include "../specialized/mesh_renderer.hpp"
// R2: draw() now consumes the slim processor context POD.
#include <chronon3d/backends/software/software_processor_context.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d::renderer {

class SoftwareMeshProcessor final : public ShapeProcessor {
public:
    void draw(const SoftwareProcessorContext& rctx, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        const auto& mesh_shape = node.shape.mesh_shape();
        if (!mesh_shape.prepared && !mesh_shape.inline_mesh) return;

        Color node_color = node.color.to_linear();
        node_color.a *= state.opacity;
        const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
        Mat4 view = camera.view_matrix();
        Mat4 projection = camera.projection_matrix(aspect);
        Mat4 mesh_model = state.projection.ready
            ? state.world_matrix : state.matrix;
        if (state.projection.ready) {
            // Scene graph placement is expressed in canvas-centered pixel
            // coordinates. The perspective camera is centered at the origin,
            // so remove the canvas offset before applying its view matrix.
            mesh_model = glm::translate(Mat4(1.0f),
                Vec3(-static_cast<f32>(width) * 0.5f,
                     -static_cast<f32>(height) * 0.5f, 0.0f)) * mesh_model;
        }
        f32 near_plane = camera.near_plane;
        f32 far_plane = camera.far_plane;
        if (state.projection.ready && state.projection.focal > 0.0f && height > 0) {
            // RenderPlan's Camera2_5D uses positive camera-space Z, while the
            // legacy mesh rasterizer and glm::perspective use negative Z.
            // Reuse the resolved RenderPlan camera and flip only that axis so
            // glTF coordinates near the origin are in front of the same
            // camera that positioned the rest of the scene.
            Mat4 flip_camera_z{1.0f};
            flip_camera_z[2][2] = -1.0f;
            view = flip_camera_z * state.projection.view;
            const f32 focal = state.projection.focal;
            const f32 fov_y = 2.0f * std::atan(
                static_cast<f32>(height) / (2.0f * focal));
            projection = glm::perspective(fov_y, aspect, near_plane, far_plane);
        }
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

        if (mesh_shape.inline_mesh) {
            if (node.inline_mesh_material) {
                const Framebuffer* base_texture = nullptr;
                if (!node.inline_mesh_texture_asset.empty()) {
                    if (!rctx.image_renderer)
                        throw std::runtime_error("textured inline mesh requires the prepared image renderer");
                    const auto image = rctx.image_renderer->find_prepared_image(node.inline_mesh_texture_asset);
                    if (!image || !image->valid() || !image->fb_img)
                        throw std::runtime_error("inline mesh texture was not prepared: " + node.inline_mesh_texture_asset);
                    base_texture = image->fb_img.get();
                }
                std::optional<assets::MeshImage> metallic_roughness;
                if (!node.inline_mesh_metallic_roughness_texture_asset.empty()) {
                    if (!rctx.image_renderer)
                        throw std::runtime_error("metallic-roughness inline mesh requires the prepared image renderer");
                    ImageDecodeOptions options;
                    options.color_space = ColorSpace::LinearSRGB;
                    const auto image = rctx.image_renderer->find_prepared_image(
                        node.inline_mesh_metallic_roughness_texture_asset, options);
                    if (!image || !image->valid() || !image->fb_img)
                        throw std::runtime_error("inline mesh metallic-roughness texture was not prepared: " +
                                                 node.inline_mesh_metallic_roughness_texture_asset);
                    metallic_roughness.emplace();
                    metallic_roughness->decoded_width = static_cast<std::uint32_t>(image->width);
                    metallic_roughness->decoded_height = static_cast<std::uint32_t>(image->height);
                    metallic_roughness->decoded_rgba.resize(static_cast<std::size_t>(image->width) * image->height * 4);
                    for (int y = 0; y < image->height; ++y) {
                        const Color* row = image->fb_img->pixels_row(y);
                        for (int x = 0; x < image->width; ++x) {
                            const auto offset = (static_cast<std::size_t>(y) * image->width + x) * 4;
                            const Color c = row[x];
                            metallic_roughness->decoded_rgba[offset + 0] = static_cast<std::uint8_t>(std::clamp(c.r, 0.f, 1.f) * 255.f + .5f);
                            metallic_roughness->decoded_rgba[offset + 1] = static_cast<std::uint8_t>(std::clamp(c.g, 0.f, 1.f) * 255.f + .5f);
                            metallic_roughness->decoded_rgba[offset + 2] = static_cast<std::uint8_t>(std::clamp(c.b, 0.f, 1.f) * 255.f + .5f);
                            metallic_roughness->decoded_rgba[offset + 3] = static_cast<std::uint8_t>(std::clamp(c.a, 0.f, 1.f) * 255.f + .5f);
                        }
                    }
                }
                chronon3d::renderer::render_mesh_filled_lit(
                    fb, *mesh_shape.inline_mesh, mesh_model, view, projection, node_color,
                    depth_buffer, state.light_context, state.rim_light, *node.inline_mesh_material,
                    near_plane, far_plane, -1.f, 0.f,
                    metallic_roughness ? &*metallic_roughness : nullptr, base_texture);
            } else if (!node.inline_mesh_texture_asset.empty()) {
                if (!rctx.image_renderer)
                    throw std::runtime_error("textured inline mesh requires the prepared image renderer");
                const auto image = rctx.image_renderer->find_prepared_image(
                    node.inline_mesh_texture_asset);
                if (!image || !image->valid() || !image->fb_img)
                    throw std::runtime_error("inline mesh texture was not prepared: " +
                                             node.inline_mesh_texture_asset);
                render_mesh_textured(fb, *mesh_shape.inline_mesh, mesh_model, view, projection,
                    node_color, depth_buffer, *image->fb_img, near_plane, far_plane,
                    node.inline_mesh_material
                        ? node.inline_mesh_material->alpha_cutoff
                        : std::optional<f32>{});
            } else {
                chronon3d::renderer::render_mesh_filled(
                    fb, *mesh_shape.inline_mesh, mesh_model, view, projection, node_color,
                    depth_buffer, near_plane, far_plane);
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
                        fb, *part.geometry, mesh_model, view, projection, color,
                        depth_buffer, state.light_context, state.rim_light, material,
                        near_plane, far_plane,
                        roughness_factor, metallic_factor, metallic_roughness);
                } else {
                    chronon3d::renderer::render_mesh_filled(
                        fb, *part.geometry, mesh_model, view, projection, color,
                        depth_buffer, near_plane, far_plane);
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
