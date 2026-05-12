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

// Safe area as insets from the canvas edges (pixels).
struct SafeArea {
    f32 top{40.0f};
    f32 bottom{40.0f};
    f32 left{40.0f};
    f32 right{40.0f};
};

struct LayoutRules {
    bool enabled{false};

    // Pin layer anchor point to a canvas position + optional margin.
    std::optional<Anchor> pin;
    f32                   margin{0.0f};

    // Clamp the layer bounding box inside the safe area.
    bool     keep_in_safe_area{false};
    SafeArea safe_area;

    // Auto-fit text: reduce font size until the text node fits in its TextBox.
    // (Pairs with TextStyle::auto_scale — this just enables the pass.)
    bool fit_text{false};
};

} // namespace chronon3d
