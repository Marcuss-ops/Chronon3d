#pragma once

// ═════════════════════════════════════════════════════════════════════════════
//  UiNode — flexbox-like layout tree node used by motion_studio.
//
//  Each UiNode is a lightweight intermediate representation that
//  UiBuilder populates during the composition lambda and LayoutResolver
//  walks to compute the final absolute positions.  The resolver writes
//  resolved_position (top-left, in canvas pixels) and resolved_size at
//  the end; components read those via component_center() / component_size().
//
//  Coordinate convention (TopLeft + Y-down) matches SVG, Figma, CSS.
//  Chronon3d layers are center-anchored — the bridge layer in UiBuilder
//  adds size/2 to the resolver's top-left to get the center-anchored layer
//  position the renderer expects.
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::motion_studio {

enum class AxisDir   { Row, Column };
enum class AlignItem { Start, Center, End, Stretch };
enum class Justify   { Start, Center, End, SpaceBetween, SpaceAround };
enum class SizeMode  { Fixed, Percent, FitContent, FillRemaining };

struct Size2D {
    SizeMode w_mode = SizeMode::FitContent;
    SizeMode h_mode = SizeMode::FitContent;

    f32 w_value = 0.0f;   // pixels when w_mode == Fixed / FillRemaining; 0..1 percent
    f32 h_value = 0.0f;
    f32 w_min = 0.0f;
    f32 h_min = 0.0f;
    std::optional<f32> w_max;
    std::optional<f32> h_max;
    Vec2 aspect = {0.0f, 0.0f};   // (W, H); 0,0 disables
};

struct Padding {
    f32 left = 0.0f;
    f32 top = 0.0f;
    f32 right = 0.0f;
    f32 bottom = 0.0f;

    static Padding all(f32 v) { return {v, v, v, v}; }
    static Padding h(f32 v) { return {v, 0, v, 0}; }
    static Padding v(f32 v) { return {0, v, 0, v}; }
};

struct UiNode {
    std::string id;
    std::string element;            // "row" / "column" / "button" / "dashboard_card" / "input_field"

    // Layout container fields (used when element ∈ {row, column, grid}).
    AxisDir direction = AxisDir::Row;
    Justify justify = Justify::Start;
    AlignItem align = AlignItem::Start;
    Padding padding;
    f32 gap = 0.0f;
    int grid_cols = 1;              // used when element == "grid"

    // Self sizing (containers are usually FitContent; components usually Fixed).
    Size2D size;

    // Component-only fields (used when element ∈ {button, dashboard_card, input_field, …}).
    std::string text_value;
    std::string subtext_value;
    std::string placeholder_value;
    std::string typed_value;        // text typed into this component by InteractionSimulator
    f32 trend_value = 0.0f;         // dashboard_card trend percentage
    bool trend_positive = true;

    Color bg_color{0.10f, 0.12f, 0.18f, 1.0f};
    Color fg_color{0.95f, 0.96f, 1.0f, 1.0f};
    Color accent_color{0.30f, 0.55f, 1.0f, 1.0f};
    f32 radius = 12.0f;
    bool bordered = false;

    // Tree.
    UiNode* parent = nullptr;
    std::vector<std::unique_ptr<UiNode>> children;

    // Resolved by LayoutResolver (top-left of child bounding box, in canvas pixels).
    Vec2 resolved_position{0.0f, 0.0f};
    Vec2 resolved_size{0.0f, 0.0f};

    [[nodiscard]] Vec2 center() const { return resolved_position + resolved_size * 0.5f; }
    [[nodiscard]] bool is_container() const {
        return element == "row" || element == "column" || element == "grid" || element == "root";
    }
};

} // namespace chronon3d::motion_studio
