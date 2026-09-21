#include "transform_policy.hpp"

#include <algorithm>
#include <cmath>

namespace chronon3d::graph::detail {

TransformExecutionPath resolve_transform_execution_path(
    const bool require_native_gpu,
    const bool input_surface_valid,
    const bool backend_available,
    const bool surface_registry_available) noexcept {
    if (input_surface_valid && backend_available && surface_registry_available) {
        return TransformExecutionPath::NativeSurface;
    }
    return require_native_gpu
        ? TransformExecutionPath::Reject
        : TransformExecutionPath::Software;
}

TransformSourceRect normalize_transform_source_rect(
    const raster::BBox& bbox,
    const i32 source_origin_x,
    const i32 source_origin_y,
    const f32 source_width,
    const f32 source_height) noexcept {
    const i32 local_x0 = bbox.x0 - source_origin_x;
    const i32 local_y0 = bbox.y0 - source_origin_y;
    const i32 local_x1 = bbox.x1 - source_origin_x;
    const i32 local_y1 = bbox.y1 - source_origin_y;
    return TransformSourceRect{
        std::clamp(static_cast<f32>(local_x0), 0.0f, source_width),
        std::clamp(static_cast<f32>(local_y0), 0.0f, source_height),
        std::clamp(static_cast<f32>(local_x1), 0.0f, source_width),
        std::clamp(static_cast<f32>(local_y1), 0.0f, source_height)};
}

std::optional<raster::BBox> clip_transform_bounds(
    const f32 min_x,
    const f32 min_y,
    const f32 max_x,
    const f32 max_y,
    const i32 destination_origin_x,
    const i32 destination_origin_y,
    const i32 destination_width,
    const i32 destination_height,
    const std::optional<raster::BBox>& clip) noexcept {
    raster::BBox bounds{
        std::clamp(static_cast<i32>(std::floor(min_x)),
                   destination_origin_x, destination_origin_x + destination_width),
        std::clamp(static_cast<i32>(std::floor(min_y)),
                   destination_origin_y, destination_origin_y + destination_height),
        std::clamp(static_cast<i32>(std::ceil(max_x)),
                   destination_origin_x, destination_origin_x + destination_width),
        std::clamp(static_cast<i32>(std::ceil(max_y)),
                   destination_origin_y, destination_origin_y + destination_height)};
    if (clip) {
        bounds.x0 = std::max(bounds.x0, clip->x0);
        bounds.y0 = std::max(bounds.y0, clip->y0);
        bounds.x1 = std::min(bounds.x1, clip->x1);
        bounds.y1 = std::min(bounds.y1, clip->y1);
    }
    if (bounds.x0 >= bounds.x1 || bounds.y0 >= bounds.y1) return std::nullopt;
    return bounds;
}

} // namespace chronon3d::graph::detail
