#include <chronon3d/core/composition/composition_registration.hpp>
#include "content/Minimalist/minimalist_theme.hpp"

namespace chronon3d::content::minimalist {


// 1. Fade-In Minimal
Composition minimalist_fade_in() {
    return composition({.name = "MinimalistFadeIn", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("text_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.fade_in(Frame{45});
            add_text_backdrop(l);
            l.text("text", make_text_params("This is a simple fade.\nClean, readable, and elegant.\nNo distractions."));
        });
        return s.build();
    });
}

// 2. Fade & Shift Vertical
Composition minimalist_fade_shift_vertical() {
    return composition({.name = "MinimalistFadeShiftVertical", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("text_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.fade_shift_vertical(Vec3{0.0f, 25.0f, 0.0f}, Frame{45});
            add_text_backdrop(l);
            l.text("text", make_text_params("The text rises smoothly\nfrom below into its place.\nNatural flow."));
        });
        return s.build();
    });
}

// 3. Fade & Shift Horizontal
Composition minimalist_fade_shift_horizontal() {
    return composition({.name = "MinimalistFadeShiftHorizontal", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("text_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.fade_shift_horizontal(Vec3{-30.0f, 0.0f, 0.0f}, Frame{45});
            add_text_backdrop(l);
            l.text("text", make_text_params("Sliding in from the left,\nfollowing the direction of reading.\nHighly intuitive."));
        });
        return s.build();
    });
}

// 4. Focus In (sfocatura progressiva)
Composition minimalist_focus_in() {
    return composition({.name = "MinimalistFocusIn", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("text_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.focus_in(16.0f, Frame{45});
            add_text_backdrop(l);
            l.text("text", make_text_params("Passing from blurred lens\nto perfect focus.\nCinematic feeling."));
        });
        return s.build();
    });
}

// 5. Reveal from Bottom
Composition minimalist_reveal_from_bottom() {
    return composition({.name = "MinimalistRevealFromBottom", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("text_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.reveal_from_bottom(40.0f, Frame{45});
            add_text_backdrop(l);
            l.text("text", make_text_params("Born from an invisible horizon line.\nClean split from grid.\nGeometric style."));
        });
        return s.build();
    });
}

// 6. Center Split
Composition minimalist_center_split() {
    return composition({.name = "MinimalistCenterSplit", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("text_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.center_split(Frame{45});
            add_text_backdrop(l);
            l.text("text", make_text_params("Revealing from center\nopening up and down.\nPerfect symmetry."));
        });
        return s.build();
    });
}

// 7. Underline Draw
Composition minimalist_underline_draw() {
    return composition({.name = "MinimalistUnderlineDraw", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("text_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            add_text_backdrop(l);
            l.text("text", make_text_params("An underline line\ndraws right below this text.\nAccentuating meaning."));
        });
        s.layer("underline", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.position({0.0f, 90.0f, 0.0f});
            l.underline_draw(Frame{45});
            l.rect("line", {
                .size = {600.0f, 4.0f},
                .color = {0.25f, 0.58f, 1.0f, 0.8f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });
        return s.build();
    });
}

// 8. Highlight Block
Composition minimalist_highlight_block() {
    return composition({.name = "MinimalistHighlightBlock", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("highlight_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.highlight_block(Frame{45});
            l.rounded_rect("bg_block", {
                .size = {800.0f, 80.0f},
                .radius = 8.0f,
                .color = {0.25f, 0.58f, 1.0f, 0.25f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });
        s.layer("text_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            add_text_backdrop(l);
            l.text("text", make_text_params("An accent block\nhighlights behind the core phrase.\nFocused sight."));
        });
        return s.build();
    });
}

// 9. Framing Bracket
Composition minimalist_framing_bracket() {
    return composition({.name = "MinimalistFramingBracket", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("bracket", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.position({-560.0f, 0.0f, 0.0f});
            l.framing_bracket(Frame{45});
            l.rect("bar", {
                .size = {6.0f, 200.0f},
                .color = {0.25f, 0.58f, 1.0f, 0.9f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });
        s.layer("text_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            add_text_backdrop(l);
            l.text("text", make_text_params("A vertical quote bar\nisolates the quote layout.\nPremium formatting."));
        });
        return s.build();
    });
}

// 10. Word Stagger
Composition minimalist_word_stagger() {
    return composition({.name = "MinimalistWordStagger", .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("line1", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.position({0.0f, -80.0f, 0.0f});
            l.word_stagger(Frame{0}, Frame{30});
            l.text("t1", make_text_params("First line reveals first."));
        });
        s.layer("line2", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.position({0.0f, 0.0f, 0.0f});
            l.word_stagger(Frame{25}, Frame{30});
            l.text("t2", make_text_params("Second line starts soon after."));
        });
        s.layer("line3", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.position({0.0f, 80.0f, 0.0f});
            l.word_stagger(Frame{50}, Frame{30});
            l.text("t3", make_text_params("Third line finishes the block."));
        });
        return s.build();
    });
}

// 11. Scale Drop
Composition minimalist_scale_drop() {
    return composition({.name = "MinimalistScaleDrop", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("text_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.scale_drop(1.08f, Frame{45});
            add_text_backdrop(l);
            l.text("text", make_text_params("Settling down softly\nfrom a slightly larger scale.\nWeighty drop."));
        });
        return s.build();
    });
}

// 12. Tracking Breathing
Composition minimalist_tracking_breathing() {
    return composition({.name = "MinimalistTrackingBreathing", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("text_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.tracking_breathing(0.96f, Frame{120});
            add_text_backdrop(l);
            l.text("text", make_text_params("Breathing scale expansion.\nVery subtle size adjustment.\nMotion feeling."));
        });
        return s.build();
    });
}

// 13. Elegant Exit Vertical
Composition minimalist_elegant_exit_vertical() {
    return composition({.name = "MinimalistElegantExitVertical", .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("text_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.fade_in(Frame{40});
            l.elegant_exit_vertical(Vec3{0.0f, 30.0f, 0.0f}, Frame{40}); // exits after frame 90 (local time evaluate inside build)
            add_text_backdrop(l);
            l.text("text", make_text_params("This slide is here,\nbut it will slide down out of view\nas the curtain falls."));
        });
        return s.build();
    });
}

// 14. Elegant Exit Horizontal
Composition minimalist_elegant_exit_horizontal() {
    return composition({.name = "MinimalistElegantExitHorizontal", .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("text_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.fade_in(Frame{40});
            l.elegant_exit_horizontal(Vec3{40.0f, 0.0f, 0.0f}, Frame{40});
            add_text_backdrop(l);
            l.text("text", make_text_params("Watch the text carefully,\nas it slides horizontally away\nleaving empty room."));
        });
        return s.build();
    });
}

// 15. Curtain Close
Composition minimalist_curtain_close() {
    return composition({.name = "MinimalistCurtainClose", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("text_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.curtain_close(Frame{45});
            add_text_backdrop(l);
            l.text("text", make_text_params("The curtain closes down.\nThe presentation ends.\nSmooth vertical exit."));
        });
        return s.build();
    });
}

} // namespace chronon3d::content::minimalist

// Registrations are now handled by MinimalistModule in minimalist_module.cpp

