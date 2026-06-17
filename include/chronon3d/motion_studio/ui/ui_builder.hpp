#pragma once

// ═════════════════════════════════════════════════════════════════════════════
//  UiBuilder — declarative UI composer for chronon3d::motion_studio.
//
//  Usage inside a composition lambda:
//
//      SequenceDirector dir;
//      dir.beat("hero",  Frame{0});
//      dir.beat("cards", Frame{20});
//
//      UiBuilder ui(ctx);
//      ui.column(20.0f)
//          .padding(40.0f)
//          .row(40.0f)
//              .dashboard_card("revenue",
//                  { .title = "Revenue", .value = "$84,240", .trend = 12.4f })
//              .dashboard_card("users",
//                  { .title = "Active users", .value = "4,902", .trend = 3.1f })
//          .end()
//          .input_field("search", "Search…", "")
//          .button("submit", "Run");
//      ui.emit(s);
//
//  After emit(), InteractionSimulator can look up component positions via
//  `ui.component_center("submit")` etc.
//
//  Layout primitives (`row`, `column`, `grid`, `end`) maintain an internal
//  stack of container nodes; `end()` pops.  `fill()` / `fixed(...)`
//  configure the *next* child only.
//
//  The `dash_card_params` etc. structs use C++20 designated initializers
//  (matches existing code in scene_presets.hpp).
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/motion_studio/layout/layout_resolver.hpp>
#include <chronon3d/motion_studio/layout/ui_node.hpp>

#include <optional>
#include <string>
#include <vector>

namespace chronon3d::motion_studio {

// ── Component param structs (C++20 designated initialisers) ───────────

struct DashCardParams {
    std::string title   = "Title";
    std::string value   = "0";
    f32         trend   = 0.0f;          // percent, signed by trend_positive
    bool        trend_positive = true;
    std::string suffix  = "";           // e.g. "USD"
    Color       accent  = {0.30f, 0.55f, 1.0f, 1.0f};
};

struct ButtonParams {
    std::string label     = "Button";
    Color       bg_color  = {0.20f, 0.50f, 1.00f, 1.00f};
    Color       fg_color  = {1.0f,  1.0f,  1.0f,  1.0f};
    f32         radius    = 22.0f;
    f32         height    = 44.0f;
    f32         min_width = 120.0f;
};

struct InputFieldParams {
    std::string placeholder = "Type here…";
    std::string value       = "";
    Color       bg_color    = {0.08f, 0.09f, 0.12f, 0.85f};
    Color       fg_color    = {0.85f, 0.87f, 0.92f, 1.0f};
    Color       placeholder_color = {0.55f, 0.60f, 0.70f, 1.0f};
    f32         height      = 44.0f;
    f32         radius      = 10.0f;
    f32         min_width   = 240.0f;
};

// ── UiBuilder ──────────────────────────────────────────────────────────

class UiBuilder {
public:
    /// Construct against a FrameContext to read canvas size + fps for later
    /// initial sizing hints.
    explicit UiBuilder(const chronon3d::FrameContext& ctx);

    // ── Stack manipulators (containers) ───────────────────────────────

    UiBuilder& row(f32 gap = 0.0f);
    UiBuilder& column(f32 gap = 0.0f);
    UiBuilder& grid(int cols, f32 gap = 0.0f);
    UiBuilder& end();

    // ── Padding (on the current container) ───────────────────────────

    UiBuilder& padding(f32 all_sides);
    UiBuilder& padding(f32 v, f32 h);
    UiBuilder& padding(f32 top, f32 right, f32 bottom, f32 left);

    // ── Sizing modifiers for the *next* added component ──────────────

    UiBuilder& fill();                              // size = FillRemaining on both axes
    UiBuilder& fill_h();                            // horizontal only
    UiBuilder& fill_v();                            // vertical   only
    UiBuilder& fixed(f32 w, f32 h);                 // explicit pixel size

    // ── Components ───────────────────────────────────────────────────

    UiBuilder& button(const std::string& id, const std::string& label);
    UiBuilder& button(const std::string& id, const ButtonParams& p);

    UiBuilder& dashboard_card(const std::string& id, const DashCardParams& p);

    UiBuilder& input_field(const std::string& id,
                           const std::string& placeholder,
                           const std::string& value = "");

    // ── Final emission ───────────────────────────────────────────────

    /// Compute layout + emit one Chronon `layer` per component into `s`.
    /// Must be called after all component calls and before `s.build()`.
    void emit(chronon3d::SceneBuilder& s);

    // ── Inspector (for cursor / interaction resolution) ──────────────

    struct ComponentRect {
        std::string id;
        chronon3d::Vec2 center;     // canvas pixels, scene center origin
        chronon3d::Vec2 size;
        std::string element;
    };

    [[nodiscard]] const std::vector<ComponentRect>& component_rects() const { return components_; }

    [[nodiscard]] std::optional<chronon3d::Vec2> component_center(const std::string& id) const;
    [[nodiscard]] std::optional<chronon3d::Vec2> component_size  (const std::string& id) const;
    [[nodiscard]] std::optional<ComponentRect>  component_rect  (const std::string& id) const;

private:
    // Helpers (declared in header, defined in cpp).
    UiNode* push(const std::string& id, const std::string& element);
    void    apply_next_size(UiNode& child) const;

    chronon3d::FrameContext       ctx_;
    LayoutResolver                resolver_;
    std::unique_ptr<UiNode>       root_;
    std::vector<UiNode*>          stack_;
    Size2D                        next_size_{};
    bool                          has_next_size_ = false;
    std::vector<ComponentRect>    components_;
};

} // namespace chronon3d::motion_studio
