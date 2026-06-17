#pragma once

// ═════════════════════════════════════════════════════════════════════════════
//  InteractionSimulator — cursor + click + type-text for SaaS demos.
//
//  Slice scope:
//    • cursor()           — initialise cursor at given layer id
//    • move_to(id, frames) — interpolate from current pos to component id
//    • click()            — emit a click-ring effect on the next layer
//    • type_text(s, frames) — animated text replacement on the most-recent
//                             input field component
//
//  Inspector-based: the simulator queries the UiBuilder for component
//  positions.  Order matters: `ui.emit(s)` must be called BEFORE
//  `cursor.emit(...)` so resolved positions are available.
//
//  Cursor visual: a small filled path (triangle) + a filled circle for the
//  click ring.  Kept tiny (10×10 px) for clarity.
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/motion_studio/ui/ui_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <string>
#include <vector>

namespace chronon3d::motion_studio {

class CursorBuilder {
public:
    explicit CursorBuilder(chronon3d::SceneBuilder& s,
                           const UiBuilder& ui,
                           const std::string& cursor_id = "cursor");

    /// Move from current point to the centre of `target_component`
    /// over `duration` frames.  Returns *this for chaining.
    CursorBuilder& move_to(const std::string& target_component, chronon3d::Frame duration);

    /// Emit a click-ring pulse at the current cursor position.  Duration is
    /// hard-coded (8 frames in the slice).
    CursorBuilder& click();

    /// Render text into the most-recent input-target component.  The
    /// animation reveals characters over `duration` frames.  Repeated
    /// calls replace the previous target text.
    CursorBuilder& type_text(const std::string& text, chronon3d::Frame duration);

    /// Finalise: emit the cursor layer + click rings + typed text into `s`.
    /// Must be called last.
    void emit();

    [[nodiscard]] const std::string& cursor_id() const { return cursor_id_; }

private:
    chronon3d::SceneBuilder& s_;
    const UiBuilder&        ui_;
    std::string             cursor_id_;
    chronon3d::Frame        cursor_start_frame_{0};
    chronon3d::Vec2         cursor_pos_{0.0f, 0.0f};
    bool                    cursor_initialized_ = false;

    struct MoveStep {
        std::string        target_component;
        chronon3d::Vec2    from_pos{0.0f, 0.0f};
        chronon3d::Vec2    to_pos{0.0f, 0.0f};
        chronon3d::Frame   start_frame{0};
        chronon3d::Frame   duration{1};
    };

    struct ClickStep {
        chronon3d::Vec2    pos{0.0f, 0.0f};
        chronon3d::Frame   start_frame{0};
    };

    struct TypeStep {
        std::string        component_id;
        std::string        text;
        chronon3d::Frame   start_frame{0};
        chronon3d::Frame   duration{1};
    };

    std::vector<MoveStep> moves_;
    std::vector<ClickStep> clicks_;
    std::vector<TypeStep> types_;
};

} // namespace chronon3d::motion_studio
