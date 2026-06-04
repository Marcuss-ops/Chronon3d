#include <chronon3d/scene/builders/commands/motion_preset_commands.hpp>
#include <chronon3d/scene/builders/layer_command_registry.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d {

// ── Motion preset commands ────────────────────────────────────────────
// These are parameterless convenience versions of the LayerBuilder motion
// presets, registered as LayerCommand instances for use by extension modules.
// For full parameter control, use the LayerBuilder:: methods directly.
//
// Note: these commands use default positions (0,0,0) as animation targets.
// The LayerBuilder methods use the layer's actual transform.position.

namespace {

// ── slide_in ──────────────────────────────────────────────────────────

struct SlideInCommand final : public LayerCommand {
    std::string_view id() const override { return "motion:slide_in"; }
    std::string_view category() const override { return "motion_preset"; }
    std::string_view display_name() const override { return "Slide In"; }
    void apply(LayerBuilder& lb) const override {
        // Default: slide from left with current position as target.
        auto& pos = lb.position_anim();
        pos.key(Frame{0}, Vec3{-200.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
        pos.key(Frame{30}, Vec3{0, 0, 0});

        auto& op = lb.opacity_anim();
        op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
        op.key(Frame{30}, 1.0f);
    }
};

// ── soft_pop ──────────────────────────────────────────────────────────

struct SoftPopCommand final : public LayerCommand {
    std::string_view id() const override { return "motion:soft_pop"; }
    std::string_view category() const override { return "motion_preset"; }
    std::string_view display_name() const override { return "Soft Pop"; }
    void apply(LayerBuilder& lb) const override {
        auto& sc = lb.scale_anim();
        sc.key(Frame{0}, Vec3{0.90f, 0.90f, 1.0f}, EasingCurve{Easing::OutBack});
        sc.key(Frame{30}, Vec3{1.0f, 1.0f, 1.0f});

        auto& op = lb.opacity_anim();
        op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
        op.key(Frame{30}, 1.0f);
    }
};

// ── float_idle ────────────────────────────────────────────────────────

struct FloatIdleCommand final : public LayerCommand {
    std::string_view id() const override { return "motion:float_idle"; }
    std::string_view category() const override { return "motion_preset"; }
    std::string_view display_name() const override { return "Float Idle"; }
    void apply(LayerBuilder& lb) const override {
        auto& pos = lb.position_anim();
        pos.loop_mode(LoopMode::Loop);

        const int period = 120;
        const f32 amplitude_y = 12.0f;
        for (int f = 0; f <= period; ++f) {
            const f32 phase = static_cast<f32>(f) / static_cast<f32>(period);
            const f32 wave = std::sin(phase * 6.2831853071795864769f);
            const f32 snapped_y = std::round(wave * amplitude_y);
            pos.key(Frame{f}, Vec3{0.0f, snapped_y, 0.0f});
        }
    }
};

// ── depth_reveal ──────────────────────────────────────────────────────

struct DepthRevealCommand final : public LayerCommand {
    std::string_view id() const override { return "motion:depth_reveal"; }
    std::string_view category() const override { return "motion_preset"; }
    std::string_view display_name() const override { return "Depth Reveal"; }
    void apply(LayerBuilder& lb) const override {
        lb.enable_3d();

        auto& pos = lb.position_anim();
        pos.key(Frame{0}, Vec3{0.0f, 0.0f, 260.0f}, EasingCurve{Easing::OutCubic});
        pos.key(Frame{45}, Vec3{0.0f, 0.0f, 0.0f});

        auto& sc = lb.scale_anim();
        sc.key(Frame{0}, Vec3{0.94f, 0.94f, 1.0f}, EasingCurve{Easing::OutCubic});
        sc.key(Frame{45}, Vec3{1.0f, 1.0f, 1.0f});

        auto& op = lb.opacity_anim();
        op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
        op.key(Frame{45}, 1.0f);
    }
};

// ── settle ────────────────────────────────────────────────────────────

struct SettleCommand final : public LayerCommand {
    std::string_view id() const override { return "motion:settle"; }
    std::string_view category() const override { return "motion_preset"; }
    std::string_view display_name() const override { return "Settle"; }
    void apply(LayerBuilder& lb) const override {
        auto& sc = lb.scale_anim();
        sc.key(Frame{0}, Vec3{1.08f, 1.08f, 1.0f}, EasingCurve{Easing::OutBack});
        sc.key(Frame{20}, Vec3{1.0f, 1.0f, 1.0f});

        auto& rot = lb.rotate_anim();
        rot.key(Frame{0}, Vec3{0.0f, 0.0f, 2.0f}, EasingCurve{Easing::OutBack});
        rot.key(Frame{20}, Vec3{0.0f, 0.0f, 0.0f});

        auto& pos = lb.position_anim();
        pos.key(Frame{0}, Vec3{0.0f, 8.0f, 0.0f}, EasingCurve{Easing::OutBack});
        pos.key(Frame{20}, Vec3{0.0f, 0.0f, 0.0f});
    }
};

// ── fade_in ───────────────────────────────────────────────────────────

struct FadeInCommand final : public LayerCommand {
    std::string_view id() const override { return "motion:fade_in"; }
    std::string_view category() const override { return "motion_preset"; }
    std::string_view display_name() const override { return "Fade In"; }
    void apply(LayerBuilder& lb) const override {
        auto& op = lb.opacity_anim();
        op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
        op.key(Frame{30}, 1.0f);
    }
};

// ── focus_in ──────────────────────────────────────────────────────────

struct FocusInCommand final : public LayerCommand {
    std::string_view id() const override { return "motion:focus_in"; }
    std::string_view category() const override { return "motion_preset"; }
    std::string_view display_name() const override { return "Focus In"; }
    void apply(LayerBuilder& lb) const override {
        auto& bl = lb.blur_anim();
        bl.key(Frame{0}, 20.0f, EasingCurve{Easing::OutCubic});
        bl.key(Frame{30}, 0.0f);

        auto& op = lb.opacity_anim();
        op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
        op.key(Frame{30}, 1.0f);
    }
};

// ── scale_drop ────────────────────────────────────────────────────────

struct ScaleDropCommand final : public LayerCommand {
    std::string_view id() const override { return "motion:scale_drop"; }
    std::string_view category() const override { return "motion_preset"; }
    std::string_view display_name() const override { return "Scale Drop"; }
    void apply(LayerBuilder& lb) const override {
        auto& sc = lb.scale_anim();
        sc.key(Frame{0}, Vec3{1.5f, 1.5f, 1.0f}, EasingCurve{Easing::OutCubic});
        sc.key(Frame{30}, Vec3{1.0f, 1.0f, 1.0f});

        auto& op = lb.opacity_anim();
        op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
        op.key(Frame{30}, 1.0f);
    }
};

// ── reveal_from_bottom ────────────────────────────────────────────────

struct RevealFromBottomCommand final : public LayerCommand {
    std::string_view id() const override { return "motion:reveal_from_bottom"; }
    std::string_view category() const override { return "motion_preset"; }
    std::string_view display_name() const override { return "Reveal From Bottom"; }
    void apply(LayerBuilder& lb) const override {
        auto& pos = lb.position_anim();
        pos.key(Frame{0}, Vec3{0.0f, 100.0f, 0.0f}, EasingCurve{Easing::OutCubic});
        pos.key(Frame{30}, Vec3{0.0f, 0.0f, 0.0f});

        auto& op = lb.opacity_anim();
        op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
        op.key(Frame{30}, 1.0f);
    }
};

// ── center_split ──────────────────────────────────────────────────────

struct CenterSplitCommand final : public LayerCommand {
    std::string_view id() const override { return "motion:center_split"; }
    std::string_view category() const override { return "motion_preset"; }
    std::string_view display_name() const override { return "Center Split"; }
    void apply(LayerBuilder& lb) const override {
        auto& sc = lb.scale_anim();
        sc.key(Frame{0}, Vec3{1.0f, 0.0f, 1.0f}, EasingCurve{Easing::OutCubic});
        sc.key(Frame{30}, Vec3{1.0f, 1.0f, 1.0f});

        auto& op = lb.opacity_anim();
        op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
        op.key(Frame{30}, 1.0f);
    }
};

// ── underline_draw ────────────────────────────────────────────────────

struct UnderlineDrawCommand final : public LayerCommand {
    std::string_view id() const override { return "motion:underline_draw"; }
    std::string_view category() const override { return "motion_preset"; }
    std::string_view display_name() const override { return "Underline Draw"; }
    void apply(LayerBuilder& lb) const override {
        auto& sc = lb.scale_anim();
        sc.key(Frame{0}, Vec3{0.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
        sc.key(Frame{30}, Vec3{1.0f, 1.0f, 1.0f});
    }
};

// ── highlight_block ───────────────────────────────────────────────────

struct HighlightBlockCommand final : public LayerCommand {
    std::string_view id() const override { return "motion:highlight_block"; }
    std::string_view category() const override { return "motion_preset"; }
    std::string_view display_name() const override { return "Highlight Block"; }
    void apply(LayerBuilder& lb) const override {
        auto& sc = lb.scale_anim();
        sc.key(Frame{0}, Vec3{0.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
        sc.key(Frame{30}, Vec3{1.0f, 1.0f, 1.0f});

        auto& op = lb.opacity_anim();
        op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
        op.key(Frame{30}, 1.0f);
    }
};

// ── framing_bracket ───────────────────────────────────────────────────

struct FramingBracketCommand final : public LayerCommand {
    std::string_view id() const override { return "motion:framing_bracket"; }
    std::string_view category() const override { return "motion_preset"; }
    std::string_view display_name() const override { return "Framing Bracket"; }
    void apply(LayerBuilder& lb) const override {
        auto& sc = lb.scale_anim();
        sc.key(Frame{0}, Vec3{1.0f, 0.0f, 1.0f}, EasingCurve{Easing::OutCubic});
        sc.key(Frame{30}, Vec3{1.0f, 1.0f, 1.0f});
    }
};

// ── curtain_close ─────────────────────────────────────────────────────

struct CurtainCloseCommand final : public LayerCommand {
    std::string_view id() const override { return "motion:curtain_close"; }
    std::string_view category() const override { return "motion_preset"; }
    std::string_view display_name() const override { return "Curtain Close"; }
    void apply(LayerBuilder& lb) const override {
        auto& sc = lb.scale_anim();
        sc.key(Frame{0}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
        sc.key(Frame{30}, Vec3{1.0f, 0.0f, 1.0f});

        auto& op = lb.opacity_anim();
        op.key(Frame{0}, 1.0f, EasingCurve{Easing::OutCubic});
        op.key(Frame{30}, 0.0f);
    }
};

// ── card_flip_2_5d ────────────────────────────────────────────────────

struct CardFlip25DCommand final : public LayerCommand {
    std::string_view id() const override { return "motion:card_flip_2_5d"; }
    std::string_view category() const override { return "motion_preset"; }
    std::string_view display_name() const override { return "Card Flip 2.5D"; }
    void apply(LayerBuilder& lb) const override {
        lb.enable_3d();

        auto& rot = lb.rotate_anim();
        rot.key(Frame{0}, Vec3{0.0f, -90.0f, 0.0f}, EasingCurve{Easing::OutCubic});
        rot.key(Frame{60}, Vec3{0.0f, 0.0f, 0.0f});

        auto& pos = lb.position_anim();
        pos.key(Frame{0}, Vec3{0.0f, 0.0f, 240.0f}, EasingCurve{Easing::OutCubic});
        pos.key(Frame{60}, Vec3{0.0f, 0.0f, 0.0f});

        auto& op = lb.opacity_anim();
        op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
        op.key(Frame{static_cast<int>(60 * 0.6f)}, 1.0f, EasingCurve{Easing::OutCubic});
    }
};

} // anonymous namespace

// ── Registration ──────────────────────────────────────────────────────

void register_motion_preset_commands(LayerCommandRegistry& registry) {
    registry.register_command(std::make_unique<SlideInCommand>());
    registry.register_command(std::make_unique<SoftPopCommand>());
    registry.register_command(std::make_unique<FloatIdleCommand>());
    registry.register_command(std::make_unique<DepthRevealCommand>());
    registry.register_command(std::make_unique<CardFlip25DCommand>());
    registry.register_command(std::make_unique<SettleCommand>());
    registry.register_command(std::make_unique<FadeInCommand>());
    registry.register_command(std::make_unique<FocusInCommand>());
    registry.register_command(std::make_unique<ScaleDropCommand>());
    registry.register_command(std::make_unique<RevealFromBottomCommand>());
    registry.register_command(std::make_unique<CenterSplitCommand>());
    registry.register_command(std::make_unique<UnderlineDrawCommand>());
    registry.register_command(std::make_unique<HighlightBlockCommand>());
    registry.register_command(std::make_unique<FramingBracketCommand>());
    registry.register_command(std::make_unique<CurtainCloseCommand>());
}

} // namespace chronon3d
