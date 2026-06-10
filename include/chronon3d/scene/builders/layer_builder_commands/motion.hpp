#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// LayerBuilder — Motion Preset Commands
//
// Keyframe-based animation helpers that wrap common motion presets.
//
// This header is #included inside class LayerBuilder { … };
// ═════════════════════════════════════════════════════════════════════════════

    // ── Motion Presets (keyframe-based animation helpers) ─────────────────
    LayerBuilder& slide_in(Vec3 from, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& soft_pop(Frame duration = Frame{30});
    LayerBuilder& float_idle(f32 amplitude_y = 12.0f, Frame cycle = Frame{120});
    LayerBuilder& depth_reveal(f32 depth_z = 260.0f, Frame duration = Frame{45});
    LayerBuilder& card_flip_2_5d(Frame duration = Frame{60});
    LayerBuilder& settle(f32 overshoot = 0.08f, Frame duration = Frame{20});
    LayerBuilder& fade_in(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& focus_in(f32 start_blur, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& scale_drop(f32 start_scale, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& fade_shift_vertical(Vec3 offset, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& fade_shift_horizontal(Vec3 offset, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& reveal_from_bottom(f32 distance, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& center_split(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& underline_draw(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& highlight_block(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& framing_bracket(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& word_stagger(Frame delay, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& tracking_breathing(f32 scale_factor, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& elegant_exit_vertical(Vec3 offset, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& elegant_exit_horizontal(Vec3 offset, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& curtain_close(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
