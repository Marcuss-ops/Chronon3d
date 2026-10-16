#include "mesh_renderer.hpp"
#include "../rasterizers/line_rasterizer.hpp"
#include "../rasterizers/scanline_rasterizer.hpp"
#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/assets/mesh_loader.hpp>
#include <chronon3d/rendering/lighting_eval.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <span>

namespace chronon3d {
namespace renderer {
namespace {

using ViewVertex = Vec4;

// A triangle clipped against 2 planes yields at most 3 + 2 = 5 vertices
// (each Sutherland-Hodgman pass can add at most one vertex per edge
// crossing; a triangle has 3 edges, and we run 2 passes).  Fixed-size
// stack arrays avoid the per-triangle heap allocations that would
// otherwise dominate allocation churn for large meshes in the frame loop.
// Keep kMaxClipVerts ≥ 6 + (number of future clip passes) if more planes
// are ever added — the clip loops ASSERT the bound below.
constexpr usize kMaxClipVerts = 6;

struct StackPolygon {
    std::array<ViewVertex, kMaxClipVerts> verts;
    usize size{0};
};

struct LitVertex {
    ViewVertex view_position;
    Vec3 world_position;
    Vec3 normal;
    Vec2 uv;
    Color base_color;
};

struct LitStackPolygon {
    std::array<LitVertex, kMaxClipVerts> verts;
    usize size{0};
};

// Clip a view-space polygon against one half-space. The signed distance
// function must be non-negative for points inside the half-space.
bool clip_polygon(StackPolygon& polygon, const auto& signed_distance) {
    if (polygon.size == 0) return true;

    std::array<ViewVertex, kMaxClipVerts> clipped;
    usize clipped_size = 0;
    for (usize i = 0; i < polygon.size; ++i) {
        const ViewVertex& a = polygon.verts[i];
        const ViewVertex& b = polygon.verts[(i + 1) % polygon.size];
        const float distance_a = signed_distance(a);
        const float distance_b = signed_distance(b);
        const bool inside_a = distance_a >= 0.0f;
        const bool inside_b = distance_b >= 0.0f;

        if (inside_a && inside_b) {
            if (clipped_size >= kMaxClipVerts) return false;
            clipped[clipped_size++] = b;
        } else if (inside_a != inside_b) {
            const float denominator = distance_a - distance_b;
            if (std::abs(denominator) > 1e-7f) {
                if (clipped_size >= kMaxClipVerts) return false;
                const float t = distance_a / denominator;
                clipped[clipped_size++] = a + t * (b - a);
            }
            if (inside_b) {
                if (clipped_size >= kMaxClipVerts) return false;
                clipped[clipped_size++] = b;
            }
        }
    }
    polygon.verts = clipped;
    polygon.size = clipped_size;
    return true;
}

bool clip_lit_polygon(LitStackPolygon& polygon, const auto& signed_distance) {
    if (polygon.size == 0) return true;
    std::array<LitVertex, kMaxClipVerts> clipped;
    usize clipped_size = 0;
    const auto interpolate = [](const LitVertex& a, const LitVertex& b, float t) {
        return LitVertex{
            a.view_position + t * (b.view_position - a.view_position),
            a.world_position + t * (b.world_position - a.world_position),
            a.normal + t * (b.normal - a.normal),
            a.uv + t * (b.uv - a.uv),
            Color{a.base_color.r + t * (b.base_color.r - a.base_color.r),
                  a.base_color.g + t * (b.base_color.g - a.base_color.g),
                  a.base_color.b + t * (b.base_color.b - a.base_color.b),
                  a.base_color.a + t * (b.base_color.a - a.base_color.a)}};
    };
    for (usize i = 0; i < polygon.size; ++i) {
        const auto& a = polygon.verts[i];
        const auto& b = polygon.verts[(i + 1) % polygon.size];
        const float da = signed_distance(a.view_position);
        const float db = signed_distance(b.view_position);
        const bool inside_a = da >= 0.0f, inside_b = db >= 0.0f;
        if (inside_a && inside_b) {
            if (clipped_size >= kMaxClipVerts) return false;
            clipped[clipped_size++] = b;
        } else if (inside_a != inside_b) {
            const float denominator = da - db;
            if (std::abs(denominator) > 1e-7f) {
                if (clipped_size >= kMaxClipVerts) return false;
                clipped[clipped_size++] = interpolate(a, b, da / denominator);
            }
            if (inside_b) {
                if (clipped_size >= kMaxClipVerts) return false;
                clipped[clipped_size++] = b;
            }
        }
    }
    for (usize i = 0; i < clipped_size; ++i) polygon.verts[i] = clipped[i];
    polygon.size = clipped_size;
    return true;
}

void render_clipped_triangle(
    Framebuffer& fb,
    const std::array<Vec4, 3>& view_triangle,
    const Mat4& projection,
    const Color& color,
    std::span<float> depth_buffer,
    float near_plane,
    float far_plane)
{
    StackPolygon polygon;
    for (usize i = 0; i < 3; ++i) polygon.verts[i] = view_triangle[i];
    polygon.size = 3;

    // GLM's perspective projection is right-handed: visible view-space Z is
    // negative. Clip before the perspective divide so w never approaches
    // zero and no behind-camera triangle can wrap around the framebuffer.
    if (!clip_polygon(polygon, [near_plane](const ViewVertex& v) {
        return -near_plane - v.z; // v.z <= -near_plane
    })) return;
    if (!clip_polygon(polygon, [far_plane](const ViewVertex& v) {
        return v.z + far_plane; // v.z >= -far_plane
    })) return;
    if (polygon.size < 3) return;

    std::array<Vec3, kMaxClipVerts> projected;
    usize projected_size = 0;
    for (usize i = 0; i < polygon.size; ++i) {
        const ViewVertex& view_vertex = polygon.verts[i];
        const float depth = -view_vertex.z;
        if (!(depth > 0.0f) || !std::isfinite(depth)) return;

        const Vec4 clip = projection * view_vertex;
        if (!(clip.w > 1e-6f) || !std::isfinite(clip.w)) return;
        const Vec2 ndc{clip.x / clip.w, clip.y / clip.w};
        if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y)) return;

        projected[projected_size++] = {
            (ndc.x + 1.0f) * 0.5f * static_cast<float>(fb.width()),
            (1.0f - (ndc.y + 1.0f) * 0.5f) * static_cast<float>(fb.height()),
            1.0f / depth,
        };
    }

    // Sutherland-Hodgman can turn one triangle into a quad. Fan triangulation
    // preserves the clipped polygon and keeps the rasterizer triangle-based.
    for (usize i = 1; i + 1 < projected_size; ++i) {
        Vec3 triangle[3] = {projected[0], projected[i], projected[i + 1]};
        fill_triangle_perspective(fb, triangle, color, depth_buffer);
    }
}

void render_clipped_lit_triangle(
    Framebuffer& fb, const std::array<LitVertex, 3>& triangle,
    const ProjectionContext& projection, std::span<float> depth_buffer,
    float near_plane,
    const rendering::LightContext& light,
    const Material2_5D& source_material,
    const rendering::RimLight* rim,
    float roughness_factor, float metallic_factor,
    const assets::MeshImage* roughness_map) {
    LitStackPolygon polygon;
    for (usize i = 0; i < 3; ++i) polygon.verts[i] = triangle[i];
    polygon.size = 3;
    // 2.5D contract: only the near depth clips, matching ProjectionContext.
    if (!clip_lit_polygon(polygon, [near_plane](const ViewVertex& v) { return v.z - near_plane; })) return;
    if (polygon.size < 3) return;

    std::array<Vec3, kMaxClipVerts> projected;
    for (usize i = 0; i < polygon.size; ++i) {
        const auto& vertex = polygon.verts[i];
        const float depth = vertex.view_position.z;
        if (!(depth > 0.0f) || !std::isfinite(depth)) return;
        const Vec2 screen = projection.view_to_screen(
            Vec3{vertex.view_position.x, vertex.view_position.y, depth});
        if (!std::isfinite(screen.x) || !std::isfinite(screen.y)) return;
        projected[i] = {screen.x, screen.y, 1.0f / depth};
    }
    const auto sample_roughness = [&](Vec2 uv) {
        if (!roughness_map || roughness_map->decoded_rgba.empty() ||
            roughness_map->decoded_width == 0 || roughness_map->decoded_height == 0)
            return 1.0f;
        const float u = uv.x - std::floor(uv.x);
        const float v = uv.y - std::floor(uv.y);
        const float px = u * static_cast<float>(roughness_map->decoded_width - 1);
        const float py = v * static_cast<float>(roughness_map->decoded_height - 1);
        const auto x0 = static_cast<std::uint32_t>(std::floor(px));
        const auto y0 = static_cast<std::uint32_t>(std::floor(py));
        const auto x1 = std::min(x0 + 1, roughness_map->decoded_width - 1);
        const auto y1 = std::min(y0 + 1, roughness_map->decoded_height - 1);
        const float tx = px - static_cast<float>(x0), ty = py - static_cast<float>(y0);
        const auto green = [&](std::uint32_t x, std::uint32_t y) {
            const auto index = (static_cast<std::size_t>(y) * roughness_map->decoded_width + x) * 4 + 1;
            return static_cast<float>(roughness_map->decoded_rgba[index]) / 255.0f;
        };
        const float top = green(x0, y0) + tx * (green(x1, y0) - green(x0, y0));
        const float bottom = green(x0, y1) + tx * (green(x1, y1) - green(x0, y1));
        return top + ty * (bottom - top);
    };
    const auto edge = [](const Vec2& a, const Vec2& b, const Vec2& p) {
        return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
    };
    for (usize i = 1; i + 1 < polygon.size; ++i) {
        const std::array<usize, 3> ids{0, i, i + 1};
        const Vec2 a{projected[ids[0]].x, projected[ids[0]].y};
        const Vec2 b{projected[ids[1]].x, projected[ids[1]].y};
        const Vec2 c{projected[ids[2]].x, projected[ids[2]].y};
        const float area = edge(a, b, c);
        if (std::abs(area) < 1e-6f) continue;
        const int x0 = std::max(0, static_cast<int>(std::ceil(std::min({a.x, b.x, c.x}))));
        const int x1 = std::min(fb.width() - 1, static_cast<int>(std::floor(std::max({a.x, b.x, c.x}))));
        const int y0 = std::max(0, static_cast<int>(std::ceil(std::min({a.y, b.y, c.y}))));
        const int y1 = std::min(fb.height() - 1, static_cast<int>(std::floor(std::max({a.y, b.y, c.y}))));
        for (int y = y0; y <= y1; ++y) {
            Color* row = fb.pixels_row(y);
            for (int x = x0; x <= x1; ++x) {
                const Vec2 p{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
                const float w0 = edge(b, c, p) / area;
                const float w1 = edge(c, a, p) / area;
                const float w2 = edge(a, b, p) / area;
                if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f) continue;
                const float z0 = projected[ids[0]].z, z1 = projected[ids[1]].z, z2 = projected[ids[2]].z;
                const float inv_depth = w0 * z0 + w1 * z1 + w2 * z2;
                if (!(inv_depth > 0.0f)) continue;
                const float depth = 1.0f / inv_depth;
                const auto depth_index = static_cast<std::size_t>(y) * fb.width() + x;
                const bool use_depth = depth_buffer.size() == static_cast<std::size_t>(fb.width()) * fb.height();
                if (use_depth && depth_buffer[depth_index] > 0.0f && depth >= depth_buffer[depth_index]) continue;
                const float q0 = w0 * z0 / inv_depth, q1 = w1 * z1 / inv_depth, q2 = w2 * z2 / inv_depth;
                const auto& v0 = polygon.verts[ids[0]];
                const auto& v1 = polygon.verts[ids[1]];
                const auto& v2 = polygon.verts[ids[2]];
                const Vec3 normal = rendering::safe_normalize(q0 * v0.normal + q1 * v1.normal + q2 * v2.normal);
                const Vec2 uv = q0 * v0.uv + q1 * v1.uv + q2 * v2.uv;
                const Color base{
                    q0 * v0.base_color.r + q1 * v1.base_color.r + q2 * v2.base_color.r,
                    q0 * v0.base_color.g + q1 * v1.base_color.g + q2 * v2.base_color.g,
                    q0 * v0.base_color.b + q1 * v1.base_color.b + q2 * v2.base_color.b,
                    q0 * v0.base_color.a + q1 * v1.base_color.a + q2 * v2.base_color.a};
                Material2_5D material = source_material;
                if (roughness_factor >= 0.0f) {
                    const float roughness = std::clamp(
                        roughness_factor * sample_roughness(uv), 0.04f, 1.0f);
                    material.shininess = std::clamp(2.0f / (roughness * roughness) - 2.0f, 1.0f, 256.0f);
                    material.specular *= 0.04f * (1.0f - metallic_factor) + metallic_factor;
                }
                const Color src = rendering::evaluate_lighting(base, normal, light, material, rim);
                if (src.a <= 0.0f || !std::isfinite(src.r) || !std::isfinite(src.g) ||
                    !std::isfinite(src.b) || !std::isfinite(src.a)) continue;
                if (use_depth) depth_buffer[depth_index] = depth;
                Color& dst = row[x];
                const float inverse_alpha = 1.0f - src.a;
                dst.r = src.r + dst.r * inverse_alpha;
                dst.g = src.g + dst.g * inverse_alpha;
                dst.b = src.b + dst.b * inverse_alpha;
                dst.a = src.a + dst.a * inverse_alpha;
            }
        }
    }
}

} // namespace

void render_mesh_wireframe(
    Framebuffer& fb, const Mesh& mesh, const Mat4& model,
    const Mat4& view, const Mat4& proj, const Color& color)
{
    const Mat4 mvp = proj * view * model;
    const auto& vertices = mesh.vertices();
    const auto& indices  = mesh.indices();

    std::vector<Vec3> projected(vertices.size());
    for (usize i = 0; i < vertices.size(); ++i) {
        Vec4 p = mvp * Vec4(vertices[i].position, 1.0f);
        if (p.w != 0.0f) { p.x /= p.w; p.y /= p.w; }
        projected[i] = {(p.x + 1.0f) * 0.5f * fb.width(),
                        (1.0f - (p.y + 1.0f) * 0.5f) * fb.height(), 0.0f};
    }
    for (usize i = 0; i + 2 < indices.size(); i += 3) {
        bline(fb, Vec2(projected[indices[i]]),   Vec2(projected[indices[i+1]]), color);
        bline(fb, Vec2(projected[indices[i+1]]), Vec2(projected[indices[i+2]]), color);
        bline(fb, Vec2(projected[indices[i+2]]), Vec2(projected[indices[i]]),   color);
    }
}

void render_mesh_filled(
    Framebuffer& fb, const Mesh& mesh, const Mat4& model,
    const Mat4& view, const Mat4& proj, const Color& color,
    std::span<float> depth_buffer, f32 near_plane, f32 far_plane)
{
    const auto& vertices = mesh.vertices();
    const auto& indices  = mesh.indices();
    if (vertices.empty() || indices.size() < 3) return;

    // The caller owns the frame-local depth buffer so multiple mesh parts
    // participate in one depth ordering. The depth buffer stores positive
    // camera-space distance; the perspective rasterizer receives 1 / depth.
    const Mat4 view_model = view * model;
    // Clip against the same frustum planes used to build the projection
    // matrix. The software processor passes these camera values explicitly;
    // the low-level renderer remains independent of Camera.
    near_plane = std::max(near_plane, 1e-5f);
    far_plane = std::max(far_plane, near_plane + 1e-5f);

    for (usize i = 0; i + 2 < indices.size(); i += 3) {
        const u32 ia = indices[i];
        const u32 ib = indices[i + 1];
        const u32 ic = indices[i + 2];
        if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) continue;

        const std::array<Vec4, 3> view_triangle = {
            view_model * Vec4(vertices[ia].position, 1.0f),
            view_model * Vec4(vertices[ib].position, 1.0f),
            view_model * Vec4(vertices[ic].position, 1.0f),
        };
        render_clipped_triangle(
            fb, view_triangle, proj, color, depth_buffer,
            near_plane, far_plane);
    }
}

void render_mesh_filled_projected(
    Framebuffer& fb, const Mesh& mesh, const Mat4& world,
    const ProjectionContext& projection, const Color& color,
    std::span<float> depth_buffer)
{
    const auto& vertices = mesh.vertices();
    const auto& indices = mesh.indices();
    if (vertices.empty() || indices.size() < 3) return;

    // Camera-space depth is forward-positive (2.5D contract). Clip at the same
    // near depth ProjectionContext::project_point uses, before the projection.
    constexpr f32 kNearDepth = 0.1f;
    for (usize i = 0; i + 2 < indices.size(); i += 3) {
        const u32 ids[3] = {indices[i], indices[i + 1], indices[i + 2]};
        if (ids[0] >= vertices.size() || ids[1] >= vertices.size() || ids[2] >= vertices.size()) continue;

        StackPolygon polygon;
        for (usize corner = 0; corner < 3; ++corner) {
            const Vec4 w = world * Vec4(vertices[ids[corner]].position, 1.0f);
            polygon.verts[corner] = projection.view * Vec4(w.x, w.y, w.z, 1.0f);
        }
        polygon.size = 3;
        if (!clip_polygon(polygon, [](const ViewVertex& v) { return v.z - kNearDepth; })) continue;
        if (polygon.size < 3) continue;

        std::array<Vec3, kMaxClipVerts> projected;
        usize projected_size = 0;
        for (usize k = 0; k < polygon.size; ++k) {
            const ViewVertex& v = polygon.verts[k];
            const Vec2 screen = projection.view_to_screen(Vec3{v.x, v.y, v.z});
            if (!std::isfinite(screen.x) || !std::isfinite(screen.y)) { projected_size = 0; break; }
            projected[projected_size++] = {screen.x, screen.y, 1.0f / v.z};
        }
        // Fan triangulation keeps the clipped polygon triangle-based.
        for (usize k = 1; k + 1 < projected_size; ++k) {
            Vec3 triangle[3] = {projected[0], projected[k], projected[k + 1]};
            fill_triangle_perspective(fb, triangle, color, depth_buffer);
        }
    }
}

void render_mesh_filled_lit(
    Framebuffer& fb, const Mesh& mesh, const Mat4& world,
    const ProjectionContext& projection, const Color& color,
    std::span<float> depth_buffer, const rendering::LightContext& light,
    const rendering::RimLight& rim, const RenderNode::InlineMeshMaterial& source_material,
    f32 near_plane, f32 roughness_factor,
    f32 metallic_factor, const assets::MeshImage* metallic_roughness)
{
    const auto& vertices = mesh.vertices();
    const auto& indices = mesh.indices();
    if (vertices.empty() || indices.size() < 3 || !light.enabled) return;
    Material2_5D material;
    material.accepts_lights = true;
    material.diffuse = source_material.diffuse;
    material.specular = source_material.specular;
    material.shininess = source_material.shininess;
    material.ambient_multiplier = 1.0f;
    rendering::RimLight active_rim = rim;
    if (active_rim.enabled) active_rim.intensity *= source_material.rim_intensity;
    Color base = color;
    if (source_material.base_color) {
        const Color tint = source_material.base_color->to_linear();
        base.r *= tint.r;
        base.g *= tint.g;
        base.b *= tint.b;
        base.a *= tint.a;
    }
    near_plane = std::max(near_plane, 1e-5f);
    for (usize i = 0; i + 2 < indices.size(); i += 3) {
        const u32 ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
        if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) continue;
        const std::array<u32, 3> ids{ia, ib, ic};
        const auto world_position = [&](u32 id) {
            const Vec4 p = world * Vec4(vertices[id].position, 1.0f);
            return std::abs(p.w) > 1e-6f ? Vec3{p.x / p.w, p.y / p.w, p.z / p.w} : Vec3{p};
        };
        const Vec3 p0 = world_position(ia), p1 = world_position(ib), p2 = world_position(ic);
        const Vec3 face_normal = rendering::safe_normalize(glm::cross(p1 - p0, p2 - p0));
        std::array<LitVertex, 3> triangle;
        for (usize corner = 0; corner < 3; ++corner) {
            const u32 id = ids[corner];
            const Vec3 input_normal = vertices[id].normal;
            const Vec3 normal = glm::dot(input_normal, input_normal) > 1e-8f
                ? rendering::transform_normal(world, input_normal) : face_normal;
            triangle[corner] = {
                projection.view * Vec4(world_position(id), 1.0f),
                world_position(id), normal, vertices[id].uv, base};
        }
        render_clipped_lit_triangle(fb, triangle, projection, depth_buffer,
            near_plane, light, material,
            active_rim.enabled ? &active_rim : nullptr,
            roughness_factor, metallic_factor, metallic_roughness);
    }
}

void render_mesh_filled(
    Framebuffer& fb, const Mesh& mesh, const Mat4& model,
    const Mat4& view, const Mat4& proj, const Color& color,
    f32 near_plane, f32 far_plane)
{
    std::vector<float> depth_buffer(
        static_cast<size_t>(fb.width()) * static_cast<size_t>(fb.height()), 0.0f);
    render_mesh_filled(fb, mesh, model, view, proj, color, depth_buffer,
                       near_plane, far_plane);
}

} // namespace renderer
} // namespace chronon3d
