#pragma once

#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/math/mat4.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>

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
        if (cam.z >= 0.0f) {
            ok = false;
            return {};
        }
        ok = true;
        return view_to_screen(cam);
    }

    [[nodiscard]] ProjectedPoint project_point(const Vec3& world) const {
        ProjectedPoint out;
        const Vec3 cam = to_view(world);
        if (cam.z >= 0.0f) {
            return out;
        }
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
};

using Projector2_5D = ProjectionContext;

inline ProjectionContext make_projection_context(const Camera& camera, i32 width, i32 height) {
    ProjectionContext out;
    out.view = camera.view_matrix();
    const f32 fw = static_cast<f32>(width);
    out.focal = camera.focal_length(fw);
    out.vp_cx = fw * 0.5f;
    out.vp_cy = static_cast<f32>(height) * 0.5f;
    return out;
}

template <typename Runtime>
inline void prepare_projection_context(Runtime& rt, const Camera& camera, i32 width, i32 height) {
    rt.projection = make_projection_context(camera, width, height);
    rt.projection.ready = true;
}

} // namespace chronon3d::renderer
