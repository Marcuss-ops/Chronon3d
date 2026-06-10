#pragma once

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_presets.hpp>
#include <chronon3d/presets/motion_renderer.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/animation/easing/interpolate.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../helpers/text_helpers.hpp"

namespace chronon3d::content::text::typewriter {

using presets::motion::MotionObject;
using presets::motion::MotionPreset;

struct SweepMotion {
    bool enabled{false};
    f32 amp_z{28.0f};
    f32 period{120.0f};

    [[nodiscard]] Vec3 offset(Frame frame, f32 start) const {
        if (!enabled) return {0, 0, 0};
        const f32 t = static_cast<f32>(frame) - start;
        if (t <= 0) return {0, 0, 0};
        return {0, 0, amp_z * std::sin((t / std::max(period, 1.0f)) * 6.283185f)};
    }
};

struct TypewriterLine {
    std::string text;
    Vec3 pos{0, 0, 0};
    Vec2 size{1500.0f, 120.0f};
    f32 font_size{56.0f};
    f32 tracking{0.0f};
    Color color_val{1, 1, 1, 1};
    f32 start_frame{0.0f};
    f32 chars_per_frame{2.0f};
    TextAlign align_val{TextAlign::Center};
    bool show_cursor{true};
    SweepMotion sweep_val{};

    TypewriterLine(std::string t) : text(std::move(t)) {}

    TypewriterLine& set_pos(Vec3 p) { pos = p; return *this; }
    TypewriterLine& set_size(Vec2 s) { size = s; return *this; }
    TypewriterLine& set_font(f32 s, f32 t = 0.0f) { font_size = s; tracking = t; return *this; }
    TypewriterLine& set_timing(f32 s, f32 speed = 2.0f) { start_frame = s; chars_per_frame = speed; return *this; }
    TypewriterLine& set_color(Color c) { color_val = c; return *this; }
    TypewriterLine& set_align(TextAlign a) { align_val = a; return *this; }
    TypewriterLine& set_cursor(bool on) { show_cursor = on; return *this; }
    TypewriterLine& set_sweep(f32 amp = 28.0f) { sweep_val.enabled = true; sweep_val.amp_z = amp; return *this; }

    [[nodiscard]] std::string reveal(Frame frame) const {
        const f32 t = static_cast<f32>(frame) - start_frame;
        if (t <= 0) return "";
        size_t visible = std::min(text.size(), static_cast<size_t>(t * std::max(chars_per_frame, 0.1f)));
        std::string out = text.substr(0, visible);
        if (show_cursor && visible < text.size() && (frame / 5) % 2 == 0) out += "|";
        return out;
    }
};

// ── FadeIn opacity helper ────────────────────────────────────────────────
inline f32 fade_in_opacity(Frame frame, Frame duration) {
    f32 t = std::clamp(static_cast<f32>(frame) / static_cast<f32>(std::max<Frame>(duration, 1)), 0.0f, 1.0f);
    return interpolate(t, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutCubic);
}

// ── PerspectiveSweepTextReveal opacity helper ────────────────────────────
inline f32 sweep_text_opacity(Frame frame, Frame duration) {
    f32 t = std::clamp(static_cast<f32>(frame) / static_cast<f32>(std::max<Frame>(duration, 1)), 0.0f, 1.0f);
    return interpolate(t, 0.0f, 0.18f, 0.0f, 1.0f, Easing::OutCubic);
}

// ── StaggerReveal opacity helper ─────────────────────────────────────────
inline f32 stagger_opacity(Frame frame, Frame duration) {
    f32 t = std::clamp(static_cast<f32>(frame) / static_cast<f32>(std::max<Frame>(duration, 1)), 0.0f, 1.0f);
    return interpolate(t, 0.0f, 0.18f, 0.0f, 1.0f, Easing::OutCubic);
}

// ── GlowBloom opacity helper ─────────────────────────────────────────────
inline f32 glow_bloom_opacity(Frame frame, Frame duration) {
    f32 t = std::clamp(static_cast<f32>(frame) / static_cast<f32>(std::max<Frame>(duration, 1)), 0.0f, 1.0f);
    return interpolate(t, 0.0f, 0.22f, 0.0f, 1.0f, Easing::OutCubic);
}

// ── Default opacity (no animation) ───────────────────────────────────────
inline f32 full_opacity(Frame, Frame) { return 1.0f; }

inline Composition make_typewriter(
    std::string name,
    std::vector<TypewriterLine> lines,
    MotionPreset preset = MotionPreset::PerspectiveSweepTextReveal,
    bool glow = false,
    Color bg = {0.01f, 0.012f, 0.022f, 1.0f},
    Frame duration_frames = 180,
    f32 camera_zoom = 1380.0f,
    i32 canvas_width = 1920,
    i32 canvas_height = 1080,
    std::function<Camera2_5D(f32)> camera_fn = {}
) {
    // Select opacity function based on preset
    f32 (*opacity_fn)(Frame, Frame) = &full_opacity;
    switch (preset) {
        case MotionPreset::FadeIn:
            opacity_fn = &fade_in_opacity;
            break;
        case MotionPreset::PerspectiveSweepTextReveal:
            opacity_fn = &sweep_text_opacity;
            break;
        case MotionPreset::SoftDollyReveal:
            opacity_fn = &fade_in_opacity;
            break;
        case MotionPreset::StaggerReveal:
            opacity_fn = &stagger_opacity;
            break;
        case MotionPreset::GlowBloom:
            opacity_fn = &glow_bloom_opacity;
            break;
        default:
            opacity_fn = &full_opacity;
            break;
    }

    return composition({.name = std::move(name), .width = canvas_width, .height = canvas_height, .duration = duration_frames}, 
        [lines = std::move(lines), preset, glow, bg, duration_frames, camera_zoom, canvas_width, canvas_height, camera_fn = std::move(camera_fn), opacity_fn](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = std::clamp(static_cast<f32>(ctx.frame) / static_cast<f32>(std::max<Frame>(duration_frames, 1)), 0.0f, 1.0f);
        // Camera disabled for 2D text-only compositions — using default ortho
        // (no camera setup means layers render at 1:1 pixel mapping)
        // if (camera_fn) {
        //     s.camera().set(camera_fn(p));
        // } else {
        //     s.camera().set(camera_motion::dolly_in(p, camera_zoom));
        // }

        // Background + grid in ONE cached layer — keeps the grid stable even when
        // text layer dirty rects trigger partial scene compositing.
        s.layer("bg_grid", [&, bg](auto& l) {
            l.cache_static()
             .fill(bg);
            l.grid_background("g", {
                .size = {static_cast<f32>(canvas_width), static_cast<f32>(canvas_height)},
                .grid_color = {0.18f, 0.5f, 0.96f, 0.12f},
                .spacing = 96.0f
            });
        });

        // Text layers — direct l.text() instead of MotionObject pipeline
        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& line = lines[i];
            std::string revealed = line.reveal(ctx.frame);
            if (revealed.empty()) continue;

            f32 opacity = opacity_fn(ctx.frame, duration_frames);

            s.layer("tw_" + std::to_string(i), [i, &line, revealed = std::move(revealed), opacity, frame = ctx.frame, glow](auto& l) {
                l.pin_to(Anchor::Center)
                 .position(line.pos + line.sweep_val.offset(frame, line.start_frame))
                 .opacity(opacity);

                l.text("txt", {
                    .text = std::move(revealed),
                    .size = line.size,
                    .pos = {0.0f, 0.0f, 0.0f},
                    .font_size = line.font_size,
                    .color = line.color_val,
                    .align = line.align_val,
                    .vertical_align = VerticalAlign::Middle,
                    .tracking = line.tracking,
                });

                if (glow) {
                    l.glow(8.0f, 0.6f, Color{0.6f, 0.7f, 1.0f, 0.7f});
                }
            });
        }

        return s.build();
    });
}

} // namespace chronon3d::content::text::typewriter
