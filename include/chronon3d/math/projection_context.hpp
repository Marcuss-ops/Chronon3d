#pragma once

// Minimal projection context struct — no camera/scene dependencies.
// Factory functions are in projector_2_5d.hpp (requires camera headers).

#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/math/mat4.hpp>
#include <chronon3d/rendering/projected_card.hpp>
#include <chronon3d/core/types.hpp>

namespace chronon3d::renderer {

struct ProjectedPoint {
    Vec2 screen{0.0f, 0.0f};
    f32 depth{0.0f};
    bool visible{false};
};

struct ProjectedQuad {
    Vec2 corners[4]{};
    f32 depth{0.0f};
    bool visible{false};
};

struct ProjectionContext {
    bool ready{false};
    Mat4 view{1.0f};
    f32 focal{1000.0f};
    f32 vp_cx{640.0f};
    f32 vp_cy{360.0f};

    [[nodiscard]] Vec3 to_view(const Vec3& world) const {
        Vec4 v = view * Vec4(world, 1.0f);
        return {v.x, v.y, v.z};
    }

    [[nodiscard]] Vec2 view_to_screen(const Vec3& cam) const {
        const f32 ps = focal / (-cam.z);
        return {-cam.x * ps + vp_cx, -cam.y * ps + vp_cy};
    }

    [[nodiscard]] Vec2 project(const Vec3& world, bool& ok) const {
        const Vec3 cam = to_view(world);
        if (cam.z >= 0.0f) { ok = false; return {}; }
        ok = true;
        return view_to_screen(cam);
    }

    [[nodiscard]] ProjectedPoint project_point(const Vec3& world) const {
        ProjectedPoint out;
        const Vec3 cam = to_view(world);
        if (cam.z >= 0.0f) return out;
        out.screen = view_to_screen(cam);
        out.depth = -cam.z;
        out.visible = true;
        return out;
    }

    [[nodiscard]] ProjectedQuad project_quad(const Vec3 corners_world[4]) const {
        ProjectedQuad out;
        bool visible = true;
        f32 depth_sum = 0.0f;
        for (int i = 0; i < 4; ++i) {
            const auto p = project_point(corners_world[i]);
            out.corners[i] = p.screen;
            visible = visible && p.visible;
            depth_sum += p.depth;
        }
        out.visible = visible;
        out.depth = depth_sum * 0.25f;
        return out;
    }

    [[nodiscard]] rendering::ProjectedCard project_card(const Mat4& trs, Vec2 size) const {
        const f32 hw = size.x * 0.5f;
        const f32 hh = size.y * 0.5f;
        const Vec3 local[4] = {
            {-hw, -hh, 0.0f}, { hw, -hh, 0.0f},
            { hw,  hh, 0.0f}, {-hw,  hh, 0.0f},
        };
        rendering::ProjectedCard out;
        bool visible = true;
        f32 depth_sum = 0.0f;
        for (int i = 0; i < 4; ++i) {
            Vec4 w = trs * Vec4(local[i], 1.0f);
            const auto p = project_point({w.x, w.y, w.z});
            out.corners[i] = p.screen;
            visible = visible && p.visible;
            depth_sum += p.depth;
        }
        out.visible = visible;
        out.depth = depth_sum * 0.25f;
        return out;
    }

    [[nodiscard]] bool project_line_clipped(Vec3 w0, Vec3 w1, Vec2& p0, Vec2& p1) const {
        Vec4 c0 = view * Vec4(w0, 1.0f);
        Vec4 c1 = view * Vec4(w1, 1.0f);
        constexpr f32 near = 0.5f;
        if (c0.z >= -near && c1.z >= -near) return false;
        if (c0.z >= -near) {
            const f32 t = (-near - c1.z) / (c0.z - c1.z);
            c0 = c1 + (c0 - c1) * t;
        }
        if (c1.z >= -near) {
            const f32 t = (-near - c0.z) / (c1.z - c0.z);
            c1 = c0 + (c1 - c0) * t;
        }
        const f32 ps0 = focal / (-c0.z);
        const f32 ps1 = focal / (-c1.z);
        p0 = {c0.x * ps0 + vp_cx, -c0.y * ps0 + vp_cy};
        p1 = {c1.x * ps1 + vp_cx, -c1.y * ps1 + vp_cy};
        return true;
    }
};

using Projector2_5D = ProjectionContext;

} // namespace chronon3d::renderer
