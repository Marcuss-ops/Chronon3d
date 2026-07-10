// ═══════════════════════════════════════════════════════════════════════════
// motion_preset_packs.hpp — C3: canonical pack-based motion preset registry.
//
// Replaces the pre-C3 pattern of hardcoded LayerBuilder preset methods
// (slide_in, soft_pop, fade_in, …) with a single canonical registry
// organized into named packs:
//
//   "chronon3d-motion-basic"   — entry/exit transitions (fade_in, slide_in,
//                                scale_drop, soft_pop, focus_in, reveal, …)
//   "cinematic"                — 3D/camera-aware presets (depth_reveal,
//                                card_flip_2_5d, float_idle, settle, …)
//   "text-kinetic"             — text-specific reveals (center_split,
//                                underline_draw, highlight_block, framing_bracket,
//                                word_stagger, tracking_breathing, …)
//
// Usage:
//   #include <chronon3d/presets/motion_preset_packs.hpp>
//
//   auto& reg = chronon3d::presets::motion_preset_packs();
//   reg.apply(lb, "slide_in");          // default params
//
//   // Enumerate available presets in a pack:
//   for (auto& id : reg.pack_ids("chronon3d-motion-basic")) { … }
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/scene/builders/layer_builder.hpp>

#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::presets {

// ── Pack name constants ────────────────────────────────────────────────

inline constexpr std::string_view kPackBasic      = "chronon3d-motion-basic";
inline constexpr std::string_view kPackCinematic  = "cinematic";
inline constexpr std::string_view kPackTextKinetic = "text-kinetic";

// ── MotionPresetDescriptor ─────────────────────────────────────────────

struct MotionPresetDescriptor {
    std::string id;
    std::string display_name;
    std::string pack;

    std::function<void(LayerBuilder&)> apply;
};

// ── MotionPresetPackRegistry ───────────────────────────────────────────

/// Canonical registry of motion presets organized into named packs.
///
/// Presets use hardcoded default targets (Vec3{0,0,0}, opacity 1.0f,
/// scale Vec3{1,1,1}) matching the LayerCommand convention.  For
/// custom targets, use LayerBuilder::position_anim() etc. directly.
class MotionPresetPackRegistry {
public:
    MotionPresetPackRegistry() = default;

    void register_preset(MotionPresetDescriptor preset) {
        if (m_frozen) {
            throw std::runtime_error(
                "MotionPresetPackRegistry: cannot register '" + preset.id +
                "' — registry is frozen");
        }
        if (m_presets.contains(preset.id)) {
            throw std::runtime_error(
                "MotionPresetPackRegistry: duplicate preset '" + preset.id + "'");
        }
        m_presets[preset.id] = std::move(preset);
    }

    void freeze() noexcept { m_frozen = true; }
    [[nodiscard]] bool is_frozen() const noexcept { return m_frozen; }

    void apply(LayerBuilder& lb, std::string_view preset_id) const {
        auto it = m_presets.find(preset_id);
        if (it == m_presets.end()) {
            throw std::runtime_error(
                "MotionPresetPackRegistry: unknown preset '" +
                std::string(preset_id) + "'");
        }
        it->second.apply(lb);
    }

    [[nodiscard]] std::vector<std::string> pack_ids(std::string_view pack_name) const {
        std::vector<std::string> result;
        for (const auto& [id, desc] : m_presets) {
            if (desc.pack == pack_name) result.push_back(id);
        }
        return result;
    }

    [[nodiscard]] std::vector<std::string> ids() const {
        std::vector<std::string> result;
        for (const auto& [id, _] : m_presets) result.push_back(id);
        return result;
    }

    [[nodiscard]] bool contains(std::string_view id) const {
        return m_presets.contains(id);
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_presets.size(); }
    void clear() noexcept { m_presets.clear(); m_frozen = false; }

private:
    std::map<std::string, MotionPresetDescriptor, std::less<>> m_presets;
    bool m_frozen{false};
};

// ── Seeding ────────────────────────────────────────────────────────────

namespace detail {

inline void seed_builtin_presets(MotionPresetPackRegistry& reg) {

    // ══════════════════════════════════════════════════════════════════
    // chronon3d-motion-basic — entry/exit transitions
    // ══════════════════════════════════════════════════════════════════

    reg.register_preset({.id = "slide_in", .display_name = "Slide In",
        .pack = std::string(kPackBasic),
        .apply = [](LayerBuilder& lb) {
            auto& pos = lb.position_anim();
            pos.add_keyframe(Frame{0}, Vec3{-200.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
            pos.add_keyframe(Frame{30}, Vec3{0.0f, 0.0f, 0.0f});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{30}, 1.0f);
        }});

    reg.register_preset({.id = "soft_pop", .display_name = "Soft Pop",
        .pack = std::string(kPackBasic),
        .apply = [](LayerBuilder& lb) {
            auto& sc = lb.scale_anim();
            sc.add_keyframe(Frame{0}, Vec3{0.90f, 0.90f, 1.0f}, EasingCurve{Easing::OutBack});
            sc.add_keyframe(Frame{30}, Vec3{1.0f, 1.0f, 1.0f});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{30}, 1.0f);
        }});

    reg.register_preset({.id = "fade_in", .display_name = "Fade In",
        .pack = std::string(kPackBasic),
        .apply = [](LayerBuilder& lb) {
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{30}, 1.0f);
        }});

    reg.register_preset({.id = "focus_in", .display_name = "Focus In",
        .pack = std::string(kPackBasic),
        .apply = [](LayerBuilder& lb) {
            auto& bl = lb.blur_anim();
            bl.add_keyframe(Frame{0}, 20.0f, EasingCurve{Easing::OutCubic});
            bl.add_keyframe(Frame{30}, 0.0f);
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{30}, 1.0f);
        }});

    reg.register_preset({.id = "scale_drop", .display_name = "Scale Drop",
        .pack = std::string(kPackBasic),
        .apply = [](LayerBuilder& lb) {
            auto& sc = lb.scale_anim();
            sc.add_keyframe(Frame{0}, Vec3{1.5f, 1.5f, 1.0f}, EasingCurve{Easing::OutCubic});
            sc.add_keyframe(Frame{30}, Vec3{1.0f, 1.0f, 1.0f});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{30}, 1.0f);
        }});

    reg.register_preset({.id = "reveal_from_bottom", .display_name = "Reveal From Bottom",
        .pack = std::string(kPackBasic),
        .apply = [](LayerBuilder& lb) {
            auto& pos = lb.position_anim();
            pos.add_keyframe(Frame{0}, Vec3{0.0f, 100.0f, 0.0f}, EasingCurve{Easing::OutCubic});
            pos.add_keyframe(Frame{30}, Vec3{0.0f, 0.0f, 0.0f});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{30}, 1.0f);
        }});

    reg.register_preset({.id = "fade_shift_vertical", .display_name = "Fade Shift Vertical",
        .pack = std::string(kPackBasic),
        .apply = [](LayerBuilder& lb) {
            auto& pos = lb.position_anim();
            pos.add_keyframe(Frame{0}, Vec3{0.0f, 40.0f, 0.0f}, EasingCurve{Easing::OutCubic});
            pos.add_keyframe(Frame{30}, Vec3{0.0f, 0.0f, 0.0f});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{30}, 1.0f);
        }});

    reg.register_preset({.id = "fade_shift_horizontal", .display_name = "Fade Shift Horizontal",
        .pack = std::string(kPackBasic),
        .apply = [](LayerBuilder& lb) {
            auto& pos = lb.position_anim();
            pos.add_keyframe(Frame{0}, Vec3{-60.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
            pos.add_keyframe(Frame{30}, Vec3{0.0f, 0.0f, 0.0f});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{30}, 1.0f);
        }});

    // ══════════════════════════════════════════════════════════════════
    // cinematic — 3D/camera-aware presets
    // ══════════════════════════════════════════════════════════════════

    reg.register_preset({.id = "depth_reveal", .display_name = "Depth Reveal",
        .pack = std::string(kPackCinematic),
        .apply = [](LayerBuilder& lb) {
            lb.enable_3d();
            auto& pos = lb.position_anim();
            pos.add_keyframe(Frame{0}, Vec3{0.0f, 0.0f, 260.0f}, EasingCurve{Easing::OutCubic});
            pos.add_keyframe(Frame{45}, Vec3{0.0f, 0.0f, 0.0f});
            auto& sc = lb.scale_anim();
            sc.add_keyframe(Frame{0}, Vec3{0.94f, 0.94f, 1.0f}, EasingCurve{Easing::OutCubic});
            sc.add_keyframe(Frame{45}, Vec3{1.0f, 1.0f, 1.0f});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{45}, 1.0f);
        }});

    reg.register_preset({.id = "card_flip_2_5d", .display_name = "Card Flip 2.5D",
        .pack = std::string(kPackCinematic),
        .apply = [](LayerBuilder& lb) {
            lb.enable_3d();
            auto& rot = lb.rotate_anim();
            rot.add_keyframe(Frame{0}, Vec3{0.0f, -90.0f, 0.0f}, EasingCurve{Easing::OutCubic});
            rot.add_keyframe(Frame{60}, Vec3{0.0f, 0.0f, 0.0f});
            auto& pos = lb.position_anim();
            pos.add_keyframe(Frame{0}, Vec3{0.0f, 0.0f, 240.0f}, EasingCurve{Easing::OutCubic});
            pos.add_keyframe(Frame{60}, Vec3{0.0f, 0.0f, 0.0f});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{36}, 1.0f);
        }});

    reg.register_preset({.id = "float_idle", .display_name = "Float Idle",
        .pack = std::string(kPackCinematic),
        .apply = [](LayerBuilder& lb) {
            auto& pos = lb.position_anim();
            pos.loop_mode(LoopMode::Loop);
            const int period = 120;
            const f32 amplitude = 12.0f;
            for (int f = 0; f <= period; ++f) {
                const f32 phase = static_cast<f32>(f) / static_cast<f32>(period);
                const f32 wave = std::sin(phase * 6.2831853071795864769f);
                pos.add_keyframe(Frame{f}, Vec3{0.0f, std::round(wave * amplitude), 0.0f});
            }
        }});

    reg.register_preset({.id = "settle", .display_name = "Settle",
        .pack = std::string(kPackCinematic),
        .apply = [](LayerBuilder& lb) {
            auto& sc = lb.scale_anim();
            sc.add_keyframe(Frame{0}, Vec3{1.08f, 1.08f, 1.0f}, EasingCurve{Easing::OutBack});
            sc.add_keyframe(Frame{20}, Vec3{1.0f, 1.0f, 1.0f});
            auto& rot = lb.rotate_anim();
            rot.add_keyframe(Frame{0}, Vec3{0.0f, 0.0f, 2.0f}, EasingCurve{Easing::OutBack});
            rot.add_keyframe(Frame{20}, Vec3{0.0f, 0.0f, 0.0f});
            auto& pos = lb.position_anim();
            pos.add_keyframe(Frame{0}, Vec3{0.0f, 8.0f, 0.0f}, EasingCurve{Easing::OutBack});
            pos.add_keyframe(Frame{20}, Vec3{0.0f, 0.0f, 0.0f});
        }});

    // ══════════════════════════════════════════════════════════════════
    // text-kinetic — text-specific reveals
    // ══════════════════════════════════════════════════════════════════

    reg.register_preset({.id = "center_split", .display_name = "Center Split",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb) {
            auto& sc = lb.scale_anim();
            sc.add_keyframe(Frame{0}, Vec3{1.0f, 0.0f, 1.0f}, EasingCurve{Easing::OutCubic});
            sc.add_keyframe(Frame{30}, Vec3{1.0f, 1.0f, 1.0f});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{30}, 1.0f);
        }});

    reg.register_preset({.id = "underline_draw", .display_name = "Underline Draw",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb) {
            auto& sc = lb.scale_anim();
            sc.add_keyframe(Frame{0}, Vec3{0.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
            sc.add_keyframe(Frame{30}, Vec3{1.0f, 1.0f, 1.0f});
        }});

    reg.register_preset({.id = "highlight_block", .display_name = "Highlight Block",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb) {
            auto& sc = lb.scale_anim();
            sc.add_keyframe(Frame{0}, Vec3{0.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
            sc.add_keyframe(Frame{30}, Vec3{1.0f, 1.0f, 1.0f});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{30}, 1.0f);
        }});

    reg.register_preset({.id = "framing_bracket", .display_name = "Framing Bracket",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb) {
            auto& sc = lb.scale_anim();
            sc.add_keyframe(Frame{0}, Vec3{1.0f, 0.0f, 1.0f}, EasingCurve{Easing::OutCubic});
            sc.add_keyframe(Frame{30}, Vec3{1.0f, 1.0f, 1.0f});
        }});

    reg.register_preset({.id = "word_stagger", .display_name = "Word Stagger",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb) {
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{5}, 0.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{20}, 1.0f);
        }});

    reg.register_preset({.id = "tracking_breathing", .display_name = "Tracking Breathing",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb) {
            auto& sc = lb.scale_anim();
            sc.add_keyframe(Frame{0}, Vec3{1.05f, 1.05f, 1.0f}, EasingCurve{Easing::OutCubic});
            sc.add_keyframe(Frame{30}, Vec3{1.0f, 1.0f, 1.0f});
        }});

    reg.register_preset({.id = "elegant_exit_vertical", .display_name = "Elegant Exit Vertical",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb) {
            auto& pos = lb.position_anim();
            pos.add_keyframe(Frame{0}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
            pos.add_keyframe(Frame{30}, Vec3{0.0f, 40.0f, 0.0f});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 1.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{30}, 0.0f);
        }});

    reg.register_preset({.id = "curtain_close", .display_name = "Curtain Close",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb) {
            auto& sc = lb.scale_anim();
            sc.add_keyframe(Frame{0}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
            sc.add_keyframe(Frame{30}, Vec3{1.0f, 0.0f, 1.0f});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 1.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{30}, 0.0f);
        }});

    reg.register_preset({.id = "elegant_exit_horizontal", .display_name = "Elegant Exit Horizontal",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb) {
            auto& pos = lb.position_anim();
            pos.add_keyframe(Frame{0}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
            pos.add_keyframe(Frame{30}, Vec3{-60.0f, 0.0f, 0.0f});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 1.0f, EasingCurve{Easing::OutCubic});
            op.add_keyframe(Frame{30}, 0.0f);
        }});
}

} // namespace detail

// ── Process-wide singleton ─────────────────────────────────────────────

[[nodiscard]] inline const MotionPresetPackRegistry& motion_preset_packs() {
    static const MotionPresetPackRegistry reg = []() {
        MotionPresetPackRegistry r;
        detail::seed_builtin_presets(r);
        r.freeze();
        return r;
    }();
    return reg;
}

} // namespace chronon3d::presets
