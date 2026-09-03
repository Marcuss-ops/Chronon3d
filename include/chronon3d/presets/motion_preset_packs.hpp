// ═══════════════════════════════════════════════════════════════════════════
// motion_preset_packs.hpp — canonical immutable motion preset catalog.
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
//   auto& catalog = chronon3d::presets::motion_preset_catalog();
//   reg.apply(lb, "slide_in");          // default params
//
//   // Enumerate available presets in a pack:
//   for (auto& id : reg.pack_ids("chronon3d-motion-basic")) { … }
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/animation/core/animation_track.hpp>
#include <chronon3d/presets/motion_error.hpp>
#include <chronon3d/presets/motion_parameters.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>  // std::runtime_error (kept for register_preset() — out of §5.0b scope)
#include <string>
#include <string_view>
#include <utility>
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

    std::function<void(LayerBuilder&, const MotionParameters&)> apply;
};

// ── Immutable motion preset catalog ────────────────────────────────────

/// Canonical registry of motion presets organized into named packs.
///
/// Presets use hardcoded default targets (Vec3{0,0,0}, opacity 1.0f,
/// scale Vec3{1,1,1}) matching the default motion convention.  For
/// custom targets, use LayerBuilder::position_anim() etc. directly.
class MotionPresetCatalog {
public:
    explicit MotionPresetCatalog(std::vector<MotionPresetDescriptor> presets)
        : m_presets(std::move(presets)) {}

    void apply(
        LayerBuilder& lb,
        std::string_view preset_id,
        const MotionParameters& params = {}) const {
        const auto it = std::find_if(
            m_presets.begin(), m_presets.end(),
            [preset_id](const MotionPresetDescriptor& preset) {
                return preset.id == preset_id;
            });
        if (it == m_presets.end()) {
            // §5.0b — typed-exception migration: throw MotionError
            // (subclass of std::runtime_error) instead of plain
            // std::runtime_error. Existing catch-blocks that match
            // `std::runtime_error` continue to compile + run unchanged
            // (backward-compat invariant). New callers can switch on
            // `.code` programmatically for typed recovery.
            //
            // SCOPE NOTE: `register_preset` (frozen + duplicate-id sites)
            // remains std::runtime_error — out of §5.0b scope per
            // user-spec "Migrate `apply(lb, id)`" wording. Future §5.x
            // forward-point commit will re-evaluate those sites.
            throw MotionError(MotionErrorCode::MotionPresetNotFound,
                              std::string(preset_id));
        }
        it->apply(lb, params);
    }

    [[nodiscard]] std::vector<std::string> pack_ids(std::string_view pack_name) const {
        std::vector<std::string> result;
        for (const auto& desc : m_presets) {
            if (desc.pack == pack_name) result.push_back(desc.id);
        }
        return result;
    }

    [[nodiscard]] std::vector<std::string> ids() const {
        std::vector<std::string> result;
        for (const auto& preset : m_presets) result.push_back(preset.id);
        return result;
    }

    [[nodiscard]] bool contains(std::string_view id) const {
        return std::any_of(
            m_presets.begin(), m_presets.end(),
            [id](const MotionPresetDescriptor& preset) {
                return preset.id == id;
            });
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_presets.size(); }
private:
    std::vector<MotionPresetDescriptor> m_presets;
};

// ── Seeding ────────────────────────────────────────────────────────────

namespace detail {

class MotionPresetCatalogBuilder {
public:
    void register_preset(MotionPresetDescriptor preset) {
        if (std::any_of(
                m_presets.begin(), m_presets.end(),
                [&preset](const MotionPresetDescriptor& existing) {
                    return existing.id == preset.id;
                })) {
            throw std::runtime_error(
                "MotionPresetCatalogBuilder: duplicate preset '" + preset.id + "'");
        }
        m_presets.push_back(std::move(preset));
    }

    [[nodiscard]] MotionPresetCatalog build() && {
        return MotionPresetCatalog(std::move(m_presets));
    }

private:
    std::vector<MotionPresetDescriptor> m_presets;
};

inline Frame duration_or(const MotionParameters& params, Frame value) {
    return params.duration.value_or(value);
}

inline EasingCurve easing_or(
    const MotionParameters& params,
    EasingCurve value) {
    return params.easing.value_or(value);
}

inline Vec3 vector_or(const MotionParameters& params, Vec3 value) {
    return params.vector.value_or(value);
}

inline f32 amount_or(const MotionParameters& params, f32 value) {
    return params.amount.value_or(value);
}

inline f32 scale_or(const MotionParameters& params, f32 value) {
    return params.scale.value_or(value);
}

inline Frame cycle_or(const MotionParameters& params, Frame value) {
    return params.cycle.value_or(value);
}

inline Frame delay_or(const MotionParameters& params, Frame value) {
    return params.delay.value_or(value);
}

inline void seed_builtin_presets(MotionPresetCatalogBuilder& reg) {

    // ══════════════════════════════════════════════════════════════════
    // chronon3d-motion-basic — entry/exit transitions
    // ══════════════════════════════════════════════════════════════════

    reg.register_preset({.id = "slide_in", .display_name = "Slide In",
        .pack = std::string(kPackBasic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const Vec3 from = vector_or(params, Vec3{-200.0f, 0.0f, 0.0f});
            const Frame duration = duration_or(params, Frame{30});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            // Preserve the authored target position; the preset owns only
            // the entry offset and must not reset the layer to the origin.
            const Vec3 target = lb.position_anim().evaluate(0.0);
            AnimationTrack<Vec3> pos_track;
            pos_track.from(Frame{0}, from, easing.preset).to(duration, target);
            lb.position_anim().apply_track(pos_track);

            AnimationTrack<f32> op_track;
            op_track.from(Frame{0}, 0.0f, easing.preset).to(duration, 1.0f);
            lb.opacity_anim().apply_track(op_track);
        }});

    reg.register_preset({.id = "soft_pop", .display_name = "Soft Pop",
        .pack = std::string(kPackBasic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const Frame duration = duration_or(params, Frame{30});
            AnimationTrack<Vec3> sc_track;
            sc_track.from(Frame{0},  Vec3{0.90f, 0.90f, 1.0f}, Easing::OutBack)
                    .to(duration, Vec3{1.0f, 1.0f, 1.0f});
            lb.scale_anim().apply_track(sc_track);

            AnimationTrack<f32> op_track;
            op_track.from(Frame{0}, 0.0f, Easing::OutCubic).to(duration, 1.0f);
            lb.opacity_anim().apply_track(op_track);
        }});

    reg.register_preset({.id = "fade_in", .display_name = "Fade In",
        .pack = std::string(kPackBasic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const Frame duration = duration_or(params, Frame{30});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            AnimationTrack<f32> op_track;
            op_track.from(Frame{0}, 0.0f, easing.preset).to(duration, 1.0f);
            lb.opacity_anim().apply_track(op_track);
        }});

    reg.register_preset({.id = "focus_in", .display_name = "Focus In",
        .pack = std::string(kPackBasic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const f32 start_blur = amount_or(params, 20.0f);
            const Frame duration = duration_or(params, Frame{30});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            AnimationTrack<f32> bl_track;
            bl_track.from(Frame{0}, start_blur, easing.preset).to(duration, 0.0f);
            lb.blur_anim().apply_track(bl_track);

            AnimationTrack<f32> op_track;
            op_track.from(Frame{0}, 0.0f, easing.preset).to(duration, 1.0f);
            lb.opacity_anim().apply_track(op_track);
        }});

    reg.register_preset({.id = "scale_drop", .display_name = "Scale Drop",
        .pack = std::string(kPackBasic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const f32 start_scale = scale_or(params, 1.5f);
            const Frame duration = duration_or(params, Frame{30});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            AnimationTrack<Vec3> sc_track;
            sc_track.from(Frame{0}, Vec3{start_scale, start_scale, 1.0f}, easing.preset)
                    .to(duration, Vec3{1.0f, 1.0f, 1.0f});
            lb.scale_anim().apply_track(sc_track);

            AnimationTrack<f32> op_track;
            op_track.from(Frame{0}, 0.0f, easing.preset).to(duration, 1.0f);
            lb.opacity_anim().apply_track(op_track);
        }});

    reg.register_preset({.id = "reveal_from_bottom", .display_name = "Reveal From Bottom",
        .pack = std::string(kPackBasic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const f32 distance = amount_or(params, 100.0f);
            const Frame duration = duration_or(params, Frame{30});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            const Vec3 target = lb.position_anim().evaluate(0.0);
            AnimationTrack<Vec3> pos_track;
            pos_track.from(Frame{0}, target + Vec3{0.0f, distance, 0.0f}, easing.preset)
                     .to(duration, target);
            lb.position_anim().apply_track(pos_track);

            AnimationTrack<f32> op_track;
            op_track.from(Frame{0}, 0.0f, easing.preset).to(duration, 1.0f);
            lb.opacity_anim().apply_track(op_track);
        }});

    reg.register_preset({.id = "fade_shift_vertical", .display_name = "Fade Shift Vertical",
        .pack = std::string(kPackBasic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const Vec3 offset = vector_or(params, Vec3{0.0f, 40.0f, 0.0f});
            const Frame duration = duration_or(params, Frame{30});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            const Vec3 target = lb.position_anim().evaluate(0.0);
            AnimationTrack<Vec3> pos_track;
            pos_track.from(Frame{0}, target + offset, easing.preset)
                     .to(duration, target);
            lb.position_anim().apply_track(pos_track);

            AnimationTrack<f32> op_track;
            op_track.from(Frame{0}, 0.0f, easing.preset).to(duration, 1.0f);
            lb.opacity_anim().apply_track(op_track);
        }});

    reg.register_preset({.id = "fade_shift_horizontal", .display_name = "Fade Shift Horizontal",
        .pack = std::string(kPackBasic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const Vec3 offset = vector_or(params, Vec3{-60.0f, 0.0f, 0.0f});
            const Frame duration = duration_or(params, Frame{30});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            const Vec3 target = lb.position_anim().evaluate(0.0);
            AnimationTrack<Vec3> pos_track;
            pos_track.from(Frame{0}, target + offset, easing.preset)
                     .to(duration, target);
            lb.position_anim().apply_track(pos_track);

            AnimationTrack<f32> op_track;
            op_track.from(Frame{0}, 0.0f, easing.preset).to(duration, 1.0f);
            lb.opacity_anim().apply_track(op_track);
        }});

    // ══════════════════════════════════════════════════════════════════
    // cinematic — 3D/camera-aware presets
    // ══════════════════════════════════════════════════════════════════

    reg.register_preset({.id = "depth_reveal", .display_name = "Depth Reveal",
        .pack = std::string(kPackCinematic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const f32 depth = amount_or(params, 260.0f);
            const Frame duration = duration_or(params, Frame{45});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            lb.enable_3d();
            AnimationTrack<Vec3> pos_track;
            pos_track.from(Frame{0}, Vec3{0.0f, 0.0f, depth}, easing.preset)
                     .to(duration, Vec3{0.0f, 0.0f, 0.0f});
            lb.position_anim().apply_track(pos_track);

            AnimationTrack<Vec3> sc_track;
            sc_track.from(Frame{0}, Vec3{0.94f, 0.94f, 1.0f}, easing.preset)
                    .to(duration, Vec3{1.0f, 1.0f, 1.0f});
            lb.scale_anim().apply_track(sc_track);

            AnimationTrack<f32> op_track;
            op_track.from(Frame{0}, 0.0f, easing.preset).to(duration, 1.0f);
            lb.opacity_anim().apply_track(op_track);
        }});

    reg.register_preset({.id = "card_flip_2_5d", .display_name = "Card Flip 2.5D",
        .pack = std::string(kPackCinematic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const Frame duration = duration_or(params, Frame{60});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            lb.enable_3d();
            AnimationTrack<Vec3> rot_track;
            rot_track.from(Frame{0}, Vec3{0.0f, -90.0f, 0.0f}, easing.preset)
                     .to(duration, Vec3{0.0f, 0.0f, 0.0f});
            lb.rotate_anim().apply_track(rot_track);

            AnimationTrack<Vec3> pos_track;
            pos_track.from(Frame{0}, Vec3{0.0f, 0.0f, 240.0f}, easing.preset)
                     .to(duration, Vec3{0.0f, 0.0f, 0.0f});
            lb.position_anim().apply_track(pos_track);

            AnimationTrack<f32> op_track;
            op_track.from(Frame{0}, 0.0f, easing.preset)
                    .to(Frame{static_cast<int>(duration * 0.6f)}, 1.0f);
            lb.opacity_anim().apply_track(op_track);
        }});

    reg.register_preset({.id = "float_idle", .display_name = "Float Idle",
        .pack = std::string(kPackCinematic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const int period = std::max(1, static_cast<int>(cycle_or(params, Frame{120})));
            const f32 amplitude = amount_or(params, 12.0f);
            auto& pos = lb.position_anim();
            pos.loop_mode(LoopMode::Loop);
            for (int f = 0; f <= period; ++f) {
                const f32 phase = static_cast<f32>(f) / static_cast<f32>(period);
                const f32 wave = std::sin(phase * 6.2831853071795864769f);
                pos.add_keyframe(Frame{f}, Vec3{0.0f, std::round(wave * amplitude), 0.0f});
            }
        }});

    reg.register_preset({.id = "settle", .display_name = "Settle",
        .pack = std::string(kPackCinematic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const f32 overshoot = amount_or(params, 0.08f);
            const Frame duration = duration_or(params, Frame{20});
            AnimationTrack<Vec3> sc_track;
            sc_track.from(Frame{0}, Vec3{1.0f + overshoot, 1.0f + overshoot, 1.0f}, Easing::OutBack)
                    .to(duration, Vec3{1.0f, 1.0f, 1.0f});
            lb.scale_anim().apply_track(sc_track);

            AnimationTrack<Vec3> rot_track;
            rot_track.from(Frame{0}, Vec3{0.0f, 0.0f, 2.0f}, Easing::OutBack)
                     .to(duration, Vec3{0.0f, 0.0f, 0.0f});
            lb.rotate_anim().apply_track(rot_track);

            AnimationTrack<Vec3> pos_track;
            pos_track.from(Frame{0}, Vec3{0.0f, 8.0f, 0.0f}, Easing::OutBack)
                     .to(duration, Vec3{0.0f, 0.0f, 0.0f});
            lb.position_anim().apply_track(pos_track);
        }});

    // ══════════════════════════════════════════════════════════════════
    // text-kinetic — text-specific reveals
    // ══════════════════════════════════════════════════════════════════

    reg.register_preset({.id = "center_split", .display_name = "Center Split",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const Frame duration = duration_or(params, Frame{30});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            auto& sc = lb.scale_anim();
            sc.add_keyframe(Frame{0}, Vec3{1.0f, 0.0f, 1.0f}, easing.preset);
            sc.add_keyframe(duration, Vec3{1.0f, 1.0f, 1.0f});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, easing.preset);
            op.add_keyframe(duration, 1.0f);
        }});

    reg.register_preset({.id = "underline_draw", .display_name = "Underline Draw",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const Frame duration = duration_or(params, Frame{30});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            auto& sc = lb.scale_anim();
            sc.add_keyframe(Frame{0}, Vec3{0.0f, 1.0f, 1.0f}, easing.preset);
            sc.add_keyframe(duration, Vec3{1.0f, 1.0f, 1.0f});
        }});

    reg.register_preset({.id = "highlight_block", .display_name = "Highlight Block",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const Frame duration = duration_or(params, Frame{30});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            auto& sc = lb.scale_anim();
            sc.add_keyframe(Frame{0}, Vec3{0.0f, 1.0f, 1.0f}, easing.preset);
            sc.add_keyframe(duration, Vec3{1.0f, 1.0f, 1.0f});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, easing.preset);
            op.add_keyframe(duration, 1.0f);
        }});

    reg.register_preset({.id = "framing_bracket", .display_name = "Framing Bracket",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const Frame duration = duration_or(params, Frame{30});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            auto& sc = lb.scale_anim();
            sc.add_keyframe(Frame{0}, Vec3{1.0f, 0.0f, 1.0f}, easing.preset);
            sc.add_keyframe(duration, Vec3{1.0f, 1.0f, 1.0f});
        }});

    reg.register_preset({.id = "word_stagger", .display_name = "Word Stagger",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const Frame delay = delay_or(params, Frame{5});
            const Frame duration = duration_or(params, Frame{15});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 0.0f, easing.preset);
            op.add_keyframe(delay, 0.0f, easing.preset);
            op.add_keyframe(delay + duration, 1.0f);
        }});

    reg.register_preset({.id = "tracking_breathing", .display_name = "Tracking Breathing",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const f32 scale = scale_or(params, 1.05f);
            const Frame duration = duration_or(params, Frame{30});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            auto& sc = lb.scale_anim();
            sc.add_keyframe(Frame{0}, Vec3{scale, scale, 1.0f}, easing.preset);
            sc.add_keyframe(duration, Vec3{1.0f, 1.0f, 1.0f});
        }});

    reg.register_preset({.id = "elegant_exit_vertical", .display_name = "Elegant Exit Vertical",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const Vec3 offset = vector_or(params, Vec3{0.0f, 40.0f, 0.0f});
            const Frame duration = duration_or(params, Frame{30});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            auto& pos = lb.position_anim();
            pos.add_keyframe(Frame{0}, Vec3{0.0f, 0.0f, 0.0f}, easing.preset);
            pos.add_keyframe(duration, offset);
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 1.0f, easing.preset);
            op.add_keyframe(duration, 0.0f);
        }});

    reg.register_preset({.id = "curtain_close", .display_name = "Curtain Close",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const Frame duration = duration_or(params, Frame{30});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            auto& sc = lb.scale_anim();
            sc.add_keyframe(Frame{0}, Vec3{1.0f, 1.0f, 1.0f}, easing.preset);
            sc.add_keyframe(duration, Vec3{1.0f, 0.0f, 1.0f});
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 1.0f, easing.preset);
            op.add_keyframe(duration, 0.0f);
        }});

    reg.register_preset({.id = "elegant_exit_horizontal", .display_name = "Elegant Exit Horizontal",
        .pack = std::string(kPackTextKinetic),
        .apply = [](LayerBuilder& lb, const MotionParameters& params) {
            const Vec3 offset = vector_or(params, Vec3{-60.0f, 0.0f, 0.0f});
            const Frame duration = duration_or(params, Frame{30});
            const EasingCurve easing = easing_or(params, EasingCurve{Easing::OutCubic});
            auto& pos = lb.position_anim();
            pos.add_keyframe(Frame{0}, Vec3{0.0f, 0.0f, 0.0f}, easing.preset);
            pos.add_keyframe(duration, offset);
            auto& op = lb.opacity_anim();
            op.add_keyframe(Frame{0}, 1.0f, easing.preset);
            op.add_keyframe(duration, 0.0f);
        }});
}

} // namespace detail

// ── Immutable built-in catalog ─────────────────────────────────────────

inline const MotionPresetCatalog kBuiltinMotionPresetCatalog = []() {
    detail::MotionPresetCatalogBuilder builder;
    detail::seed_builtin_presets(builder);
    return std::move(builder).build();
}();

[[nodiscard]] inline const MotionPresetCatalog& motion_preset_catalog() {
    return kBuiltinMotionPresetCatalog;
}

} // namespace chronon3d::presets

