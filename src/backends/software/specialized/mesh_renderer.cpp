#include "mesh_renderer.hpp"
#include "../rasterizers/line_rasterizer.hpp"
#include "../rasterizers/scanline_rasterizer.hpp"
#include <chronon3d/geometry/mesh.hpp>
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
    Color color;
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
        return LitVertex{a.view_position + t * (b.view_position - a.view_position),
            Color{a.color.r + t * (b.color.r - a.color.r),
                  a.color.g + t * (b.color.g - a.color.g),
                  a.color.b + t * (b.color.b - a.color.b),
                  a.color.a + t * (b.color.a - a.color.a)}};
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
    const Mat4& projection, std::span<float> depth_buffer,
    float near_plane, float far_plane) {
    LitStackPolygon polygon;
    for (usize i = 0; i < 3; ++i) polygon.verts[i] = triangle[i];
    polygon.size = 3;
    if (!clip_lit_polygon(polygon, [near_plane](const ViewVertex& v) { return -near_plane - v.z; })) return;
    if (!clip_lit_polygon(polygon, [far_plane](const ViewVertex& v) { return v.z + far_plane; })) return;
    if (polygon.size < 3) return;

    std::array<Vec3, kMaxClipVerts> projected;
    std::array<Color, kMaxClipVerts> colors;
    for (usize i = 0; i < polygon.size; ++i) {
        const auto& vertex = polygon.verts[i];
        const float depth = -vertex.view_position.z;
        if (!(depth > 0.0f) || !std::isfinite(depth)) return;
        const Vec4 clip = projection * vertex.view_position;
        if (!(clip.w > 1e-6f) || !std::isfinite(clip.w)) return;
        const Vec2 ndc{clip.x / clip.w, clip.y / clip.w};
        if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y)) return;
        projected[i] = {(ndc.x + 1.0f) * 0.5f * static_cast<float>(fb.width()),
                        (1.0f - (ndc.y + 1.0f) * 0.5f) * static_cast<float>(fb.height()),
                        1.0f / depth};
        colors[i] = vertex.color;
    }
    for (usize i = 1; i + 1 < polygon.size; ++i) {
        const std::array<Vec3, 3> positions{projected[0], projected[i], projected[i + 1]};
        const std::array<Color, 3> triangle_colors{colors[0], colors[i], colors[i + 1]};
        fill_triangle_perspective_gradient(fb, positions, triangle_colors, depth_buffer);
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

void render_mesh_filled_lit(
    Framebuffer& fb, const Mesh& mesh, const Mat4& model,
    const Mat4& view, const Mat4& proj, const Color& color,
    std::span<float> depth_buffer, const rendering::LightContext& light,
    const rendering::RimLight& rim, const RenderNode::InlineMeshMaterial& source_material,
    f32 near_plane, f32 far_plane)
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
    const Mat4 view_model = view * model;
    near_plane = std::max(near_plane, 1e-5f);
    far_plane = std::max(far_plane, near_plane + 1e-5f);
    for (usize i = 0; i + 2 < indices.size(); i += 3) {
        const u32 ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
        if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) continue;
        const std::array<u32, 3> ids{ia, ib, ic};
        const auto world_position = [&](u32 id) {
            const Vec4 p = model * Vec4(vertices[id].position, 1.0f);
            return std::abs(p.w) > 1e-6f ? Vec3{p.x / p.w, p.y / p.w, p.z / p.w} : Vec3{p};
        };
        const Vec3 p0 = world_position(ia), p1 = world_position(ib), p2 = world_position(ic);
        const Vec3 face_normal = rendering::safe_normalize(glm::cross(p1 - p0, p2 - p0));
        std::array<LitVertex, 3> triangle;
        for (usize corner = 0; corner < 3; ++corner) {
            const u32 id = ids[corner];
            const Vec3 input_normal = vertices[id].normal;
            const Vec3 normal = glm::dot(input_normal, input_normal) > 1e-8f
                ? rendering::transform_normal(model, input_normal) : face_normal;
            Color lit = glm::dot(input_normal, input_normal) > 1e-8f
                ? rendering::evaluate_lighting(base, normal, light, material,
                    active_rim.enabled ? &active_rim : nullptr)
                : base;
            lit.a *= 1.0f;
            triangle[corner] = {view_model * Vec4(vertices[id].position, 1.0f), lit};
        }
        render_clipped_lit_triangle(fb, triangle, proj, depth_buffer, near_plane, far_plane);
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
