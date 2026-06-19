#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>

namespace chronon3d::graph {

namespace detail {

inline constexpr f32 kProjectedBBoxSafetyPadding = 1.5f;

template <size_t N>
[[nodiscard]] inline std::optional<raster::BBox> project_points_bbox(
    const renderer::ProjectionContext& projection,
    const std::array<Vec3, N>& points,
    i32 width,
    i32 height,
    f32 padding = 0.0f
) {
    if (!projection.ready) {
        return std::nullopt;
    }

    f32 min_x = std::numeric_limits<f32>::max();
    f32 min_y = std::numeric_limits<f32>::max();
    f32 max_x = std::numeric_limits<f32>::lowest();
    f32 max_y = std::numeric_limits<f32>::lowest();

    for (const auto& point : points) {
        const auto projected = projection.project_point(point);
        if (!projected.visible) {
            return std::nullopt;
        }
        min_x = std::min(min_x, projected.screen.x);
        min_y = std::min(min_y, projected.screen.y);
        max_x = std::max(max_x, projected.screen.x);
        max_y = std::max(max_y, projected.screen.y);
    }

    raster::BBox bbox{
        static_cast<i32>(std::floor(min_x - padding)),
        static_cast<i32>(std::floor(min_y - padding)),
        static_cast<i32>(std::ceil(max_x + padding)),
        static_cast<i32>(std::ceil(max_y + padding))
    };
    bbox.clip_to(width, height);
    if (bbox.is_empty()) {
        return std::nullopt;
    }
    return bbox;
}

[[nodiscard]] inline std::optional<raster::BBox> projected_native_3d_bbox(
    const RenderGraphContext& ctx,
    const ::chronon3d::RenderNode& node,
    const Mat4& world_matrix,
    f32 spread = 0.0f
) {
    if (!ctx.camera.has_camera_2_5d || !ctx.camera.projection_ctx.ready) {
        return std::nullopt;
    }

    const auto expand = [spread](f32 value) {
        return value + spread;
    };

    switch (node.shape.type()) {
        case ShapeType::FakeBox3D: {
            const auto& s = node.shape.fake_box3d();
            const Vec3 center = s.world_pos;
            const f32 hw = expand(s.size.x * 0.5f);
            const f32 hh = expand(s.size.y * 0.5f);
            const f32 depth = expand(s.depth);
            const std::array<Vec3, 8> corners = {{
                {center.x - hw, center.y + hh, center.z},
                {center.x + hw, center.y + hh, center.z},
                {center.x + hw, center.y - hh, center.z},
                {center.x - hw, center.y - hh, center.z},
                {center.x - hw, center.y + hh, center.z + depth},
                {center.x + hw, center.y + hh, center.z + depth},
                {center.x + hw, center.y - hh, center.z + depth},
                {center.x - hw, center.y - hh, center.z + depth},
            }};

            std::array<Vec3, 8> transformed{};
            for (size_t i = 0; i < corners.size(); ++i) {
                const Vec4 w = world_matrix * Vec4(corners[i], 1.0f);
                transformed[i] = {w.x, w.y, w.z};
            }
            return project_points_bbox(
                ctx.camera.projection_ctx,
                transformed,
                ctx.frame.width,
                ctx.frame.height,
                spread + kProjectedBBoxSafetyPadding
            );
        }

        case ShapeType::GridPlane: {
            const auto& s = node.shape.grid_plane();
            const f32 extent = expand(s.extent);
            std::array<Vec3, 4> corners{};
            if (s.axis == PlaneAxis::XZ) {
                corners[0] = Vec3{s.world_pos.x - extent, s.world_pos.y, s.world_pos.z - extent};
                corners[1] = Vec3{s.world_pos.x + extent, s.world_pos.y, s.world_pos.z - extent};
                corners[2] = Vec3{s.world_pos.x + extent, s.world_pos.y, s.world_pos.z + extent};
                corners[3] = Vec3{s.world_pos.x - extent, s.world_pos.y, s.world_pos.z + extent};
            } else {
                corners[0] = Vec3{s.world_pos.x - extent, s.world_pos.y - extent, s.world_pos.z};
                corners[1] = Vec3{s.world_pos.x + extent, s.world_pos.y - extent, s.world_pos.z};
                corners[2] = Vec3{s.world_pos.x + extent, s.world_pos.y + extent, s.world_pos.z};
                corners[3] = Vec3{s.world_pos.x - extent, s.world_pos.y + extent, s.world_pos.z};
            }

            std::array<Vec3, 4> transformed{};
            for (size_t i = 0; i < 4; ++i) {
                const Vec4 w = world_matrix * Vec4(corners[i], 1.0f);
                transformed[i] = {w.x, w.y, w.z};
            }
            return project_points_bbox(
                ctx.camera.projection_ctx,
                transformed,
                ctx.frame.width,
                ctx.frame.height,
                spread + kProjectedBBoxSafetyPadding
            );
        }

        default:
            break;
    }

    return std::nullopt;
}

} // namespace detail

} // namespace chronon3d::graph
