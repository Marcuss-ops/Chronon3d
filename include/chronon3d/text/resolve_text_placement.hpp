#pragma once

// Canonical conversion from authoring placement intent to concrete canvas-space
// coordinates. Keep placement resolution on this free-function surface; do not
// introduce a parallel resolver class or authoring viewport type.

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/text_placement.hpp>

namespace chronon3d {

struct CanvasInfo {
    f32 width{1920.0f};
    f32 height{1080.0f};
    f32 safe_margin_top{54.0f};
    f32 safe_margin_bottom{54.0f};
    f32 safe_margin_left{96.0f};
    f32 safe_margin_right{96.0f};

    [[nodiscard]] static CanvasInfo with_safe_area(
        f32 width,
        f32 height,
        const SafeAreaPreset& preset);

    [[nodiscard]] static CanvasInfo from_dimensions(f32 width, f32 height) {
        return with_safe_area(width, height, SafeAreaPreset{});
    }
};

struct ResolvedTextPlacement {
    // Text box in canvas coordinates: x, y, width, height.
    Vec4 local_frame{0.0f, 0.0f, 0.0f, 0.0f};
    Mat4 layer_matrix{1.0f};
    Mat4 world_matrix{1.0f};
    Vec2 layout_origin{0.0f, 0.0f};
    TextAnchor resolved_anchor{TextAnchor::Center};
};

[[nodiscard]] ResolvedTextPlacement resolve_text_placement(
    const CanvasInfo& canvas,
    Vec2 box_size,
    TextPlacement placement,
    TextAnchor anchor = TextAnchor::Center,
    const Mat4& layer_matrix = Mat4(1.0f));

// Returns the canvas-space pin point where the selected text anchor is placed.
[[nodiscard]] Vec2 resolve_placement_origin(
    const CanvasInfo& canvas,
    Vec2 box_size,
    TextPlacement placement);

[[nodiscard]] TextPlacement resolve_safe_area(SafeAreaEnum side);

} // namespace chronon3d
