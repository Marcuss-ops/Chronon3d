// ═════════════════════════════════════════════════════════════════════════════
//  InteractionSimulator — CursorBuilder implementation.
//
//  Inspector pattern: the cursor walks the same UiBuilder that produced
//  the dashboard components.  Component centres come from
//  `ui.component_center(target_id)`; missing targets yield a no-op.
//
//  Cursor visual: a small filled circle (representing the click point),
//  animated through chained position keyframes.  Click rings are emitted
//  as separate Circle layers at the cursor's position.
//
//  Typed text is rendered as an overlay TextParams layer on top of the
//  input field, faded in over `duration` frames.
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/motion_studio/interaction/interaction_simulator.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>

#include <utility>

namespace chronon3d::motion_studio {

// ─── CursorBuilder ────────────────────────────────────────────────────

CursorBuilder::CursorBuilder(chronon3d::SceneBuilder& s,
                             const UiBuilder& ui,
                             const std::string& cursor_id)
    : s_(s), ui_(ui), cursor_id_(cursor_id) {}

CursorBuilder& CursorBuilder::move_to(const std::string& target_component,
                                      chronon3d::Frame duration) {
    auto target = ui_.component_center(target_component);
    if (!target) return *this;

    chronon3d::Vec2 from = cursor_initialized_ ? cursor_pos_ : *target;
    if (!cursor_initialized_) cursor_pos_ = *target;

    chronon3d::Frame start = cursor_start_frame_;
    if (cursor_initialized_) start = cursor_start_frame_;
    moves_.push_back({target_component, from, *target, start, std::max<chronon3d::Frame>(1, duration)});

    cursor_pos_         = *target;
    cursor_start_frame_ = start + std::max<chronon3d::Frame>(1, duration);
    cursor_initialized_ = true;
    return *this;
}

CursorBuilder& CursorBuilder::click() {
    if (!cursor_initialized_) return *this;
    clicks_.push_back({cursor_pos_, cursor_start_frame_});
    return *this;
}

CursorBuilder& CursorBuilder::type_text(const std::string& text,
                                        chronon3d::Frame duration) {
    if (!cursor_initialized_) return *this;
    // Most-recent input_field component id is whatever was last queryable
    // by `cursor_pos_` matching.  For simplicity we look up the closest
    // input_field component by exact id search — the showcase calls
    // `type_text` right after `move_to(search-field, …)` so cursor_pos_
    // matches; we re-derive the target id from cursor_pos_ by looking at
    // component centres that contained the cursor before this call.
    //
    // For the slice: emit a single overlay text layer above the cursor's
    // current position; the showcase positions the cursor exactly on the
    // input field center after move_to, so this self-aligns.
    types_.push_back({"__cursor_typed__", text, cursor_start_frame_,
                      std::max<chronon3d::Frame>(1, duration)});
    return *this;
}

void CursorBuilder::emit() {
    if (!cursor_initialized_) return;
    const std::string& cursor_layer_id = cursor_id_;

    // Cursor dot — small filled circle at cursor_pos_ + position_anim.
    s_.layer(cursor_layer_id + "_dot",
             [cursor_id = cursor_layer_id, moves = moves_, initial_pos = moves_.empty() ? chronon3d::Vec2{0,0} : moves_.front().from_pos]
             (chronon3d::LayerBuilder& l) {
        (void)initial_pos;
        (void)cursor_id;
        l.position({moves.empty() ? 0.0f : moves.front().from_pos.x,
                    moves.empty() ? 0.0f : moves.front().from_pos.y,
                    0.0f});

        auto& anim = l.position_anim();
        if (!moves.empty()) {
            anim.key(moves.front().start_frame,
                     moves.front().from_pos,
                     chronon3d::EasingCurve{chronon3d::Easing::OutCubic});
        }
        for (const auto& m : moves) {
            anim.key(m.start_frame + m.duration,
                     m.to_pos,
                     chronon3d::EasingCurve{chronon3d::Easing::OutCubic});
        }
        chronon3d::CircleParams cp;
        cp.radius = 7.0f;
        cp.color  = {1.0f, 1.0f, 1.0f, 1.0f};
        cp.pos    = {0, 0, 0};
        l.circle("dot", cp);
    });

    // Click rings — one layer per click, brief fade-out.
    for (std::size_t i = 0; i < clicks_.size(); ++i) {
        const auto& c = clicks_[i];
        const std::string ring_id = cursor_layer_id + "_ring_" + std::to_string(i);
        s_.layer(ring_id, [c](chronon3d::LayerBuilder& l) {
            l.position({c.pos.x, c.pos.y, 0.0f});
            chronon3d::CircleParams cp;
            cp.radius = 18.0f;
            cp.color  = {1.0f, 1.0f, 1.0f, 1.0f};
            cp.pos    = {0, 0, 0};
            l.circle("ring", cp);
            l.scale_anim()
                .key(c.start_frame,     chronon3d::Vec3{0.4f, 0.4f, 1.0f},
                     chronon3d::EasingCurve{chronon3d::Easing::OutCubic})
                .key(c.start_frame + 8, chronon3d::Vec3{1.6f, 1.6f, 1.0f},
                     chronon3d::EasingCurve{chronon3d::Easing::OutCubic});
            l.opacity_anim()
                .key(c.start_frame,     1.0f,
                     chronon3d::EasingCurve{chronon3d::Easing::OutCubic})
                .key(c.start_frame + 8, 0.0f,
                     chronon3d::EasingCurve{chronon3d::Easing::OutCubic});
        });
    }

    // Typed text — overlay text positioned 60px above the cursor's final
    // position (visually suggests a typed-into input field whose caret
    // sits at the cursor).  Position is the last completed move's target.
    for (std::size_t i = 0; i < types_.size(); ++i) {
        const auto& t = types_[i];
        const std::string overlay_id = cursor_layer_id + "_typed_" + std::to_string(i);
        const chronon3d::Vec2 final_pos = moves_.empty()
            ? chronon3d::Vec2{0.0f, 0.0f}
            : moves_.back().to_pos;
        s_.layer(overlay_id, [t, final_pos](chronon3d::LayerBuilder& l) {
            l.position({final_pos.x, final_pos.y - 60.0f, 0.0f});
            chronon3d::TextSpec tp;
            tp.content.value = t.text;
            tp.layout.box      = {320.0f, 28.0f};
            tp.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}};
            tp.font.font_size = 16.0f;
            tp.appearance.color     = {1.0f, 1.0f, 1.0f, 1.0f};
            tp.layout.align     = chronon3d::TextAlign::Center;
            tp.layout.vertical_align = chronon3d::VerticalAlign::Middle;
            tp.font.font_path = "assets/fonts/Inter-Bold.ttf";
            tp.font.font_family = "Inter";
            tp.font.font_weight = 600;
            l.text("typed", tp);
            l.opacity_anim()
                .key(t.start_frame, 0.0f,
                     chronon3d::EasingCurve{chronon3d::Easing::OutCubic})
                .key(t.start_frame + t.duration, 1.0f,
                     chronon3d::EasingCurve{chronon3d::Easing::Linear});
        });
    }
}

} // namespace chronon3d::motion_studio
