#pragma once

#include <chronon3d/math/vec2.hpp>
#include <chronon3d/core/types.hpp>
#include <optional>

namespace chronon3d {

enum class Anchor {
    TopLeft,    TopCenter,    TopRight,
    MiddleLeft, Center,       MiddleRight,
    BottomLeft, BottomCenter, BottomRight
};

struct AnchorPlacement {
    Anchor anchor{Anchor::Center};
    Vec2 offset{0.0f, 0.0f};
    std::optional<f32> depth;

    constexpr AnchorPlacement() = default;
    constexpr explicit AnchorPlacement(Anchor value, Vec2 delta = Vec2{0.0f, 0.0f}, std::optional<f32> z = std::nullopt)
        : anchor(value), offset(delta), depth(z) {}

    [[nodiscard]] constexpr AnchorPlacement offset_by(Vec2 delta) const {
        return AnchorPlacement{anchor, offset + delta, depth};
    }

    [[nodiscard]] constexpr AnchorPlacement with_depth(f32 z) const {
        return AnchorPlacement{anchor, offset, z};
    }
};

namespace Anchors {
    inline constexpr AnchorPlacement TopLeft{Anchor::TopLeft};
    inline constexpr AnchorPlacement TopCenter{Anchor::TopCenter};
    inline constexpr AnchorPlacement TopRight{Anchor::TopRight};
    inline constexpr AnchorPlacement MiddleLeft{Anchor::MiddleLeft};
    inline constexpr AnchorPlacement Center{Anchor::Center};
    inline constexpr AnchorPlacement MiddleRight{Anchor::MiddleRight};
    inline constexpr AnchorPlacement BottomLeft{Anchor::BottomLeft};
    inline constexpr AnchorPlacement BottomCenter{Anchor::BottomCenter};
    inline constexpr AnchorPlacement BottomRight{Anchor::BottomRight};
}

// Safe area as insets from the canvas edges (pixels).
struct SafeArea {
    f32 top{40.0f};
    f32 bottom{40.0f};
    f32 left{40.0f};
    f32 right{40.0f};
};

struct LayoutRules {
    bool enabled{false};

    // Pin layer anchor point to a canvas position + optional margin/offset/depth.
    std::optional<AnchorPlacement> pin;
    f32 margin{0.0f};

    // Clamp the layer bounding box inside the safe area.
    bool     keep_in_safe_area{false};
    SafeArea safe_area;

    // Auto-fit text: reduce font size until the text node fits in its TextBox.
    // (Pairs with TextStyle::auto_scale — this just enables the pass.)
    bool fit_text{false};
};

} // namespace chronon3d
