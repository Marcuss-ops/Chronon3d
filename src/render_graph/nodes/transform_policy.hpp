#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/raster_utils.hpp>

#include <optional>

namespace chronon3d::graph::detail {

enum class TransformExecutionPath {
    Software,
    NativeSurface,
    Reject,
};

// Resolve the capability gate before the transform node allocates resources
// or dispatches a backend operation.  A native transform is possible only
// when all three runtime resources are present; strict mode turns the same
// missing capability into an explicit rejection instead of a CPU fallback.
TransformExecutionPath resolve_transform_execution_path(
    bool require_native_gpu,
    bool input_surface_valid,
    bool backend_available,
    bool surface_registry_available) noexcept;

struct TransformSourceRect {
    f32 x0{0.0f};
    f32 y0{0.0f};
    f32 x1{0.0f};
    f32 y1{0.0f};

    [[nodiscard]] bool valid() const noexcept { return x0 < x1 && y0 < y1; }
};

/// Normalize a canvas-space input bbox into the source framebuffer's local
/// coordinates. The execute and predicted-bbox paths use this same contract
/// so oversized/offset bboxes cannot produce different sampling regions.
[[nodiscard]] TransformSourceRect normalize_transform_source_rect(
    const raster::BBox& bbox,
    i32 source_origin_x,
    i32 source_origin_y,
    f32 source_width,
    f32 source_height) noexcept;

/// Clamp a projected transform bounds to the destination surface and optional
/// canvas clip. Returns nullopt when the projected region is empty.
[[nodiscard]] std::optional<raster::BBox> clip_transform_bounds(
    f32 min_x,
    f32 min_y,
    f32 max_x,
    f32 max_y,
    i32 destination_origin_x,
    i32 destination_origin_y,
    i32 destination_width,
    i32 destination_height,
    const std::optional<raster::BBox>& clip) noexcept;

} // namespace chronon3d::graph::detail
