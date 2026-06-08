#pragma once

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_presets.hpp>
#include <chronon3d/presets/motion_renderer.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
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
    f32 font_size{56.0f};
    f32 tracking{0.0f};
    Color color_val{1, 1, 1, 1};
    f32 start_frame{0.0f};
    f32 chars_per_frame{2.0f};
    TextAlign align_val{TextAlign::Center};
    SweepMotion sweep_val{};

    TypewriterLine(std::string t) : text(std::move(t)) {}

    TypewriterLine& set_pos(Vec3 p) { pos = p; return *this; }
    TypewriterLine& set_font(f32 s, f32 t = 0.0f) { font_size = s; tracking = t; return *this; }
    TypewriterLine& set_timing(f32 s, f32 speed = 2.0f) { start_frame = s; chars_per_frame = speed; return *this; }
    TypewriterLine& set_color(Color c) { color_val = c; return *this; }
    TypewriterLine& set_align(TextAlign a) { align_val = a; return *this; }
    TypewriterLine& set_sweep(f32 amp = 28.0f) { sweep_val.enabled = true; sweep_val.amp_z = amp; return *this; }

    [[nodiscard]] std::string reveal(Frame frame) const {
        const f32 t = static_cast<f32>(frame) - start_frame;
        if (t <= 0) return "";
        size_t visible = std::min(text.size(), static_cast<size_t>(t * std::max(chars_per_frame, 0.1f)));
        std::string out = text.substr(0, visible);
        if (visible < text.size() && (frame / 5) % 2 == 0) out += "|";
        return out;
    }
};

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
    return composition({.name = std::move(name), .width = canvas_width, .height = canvas_height, .duration = duration_frames}, [lines = std::move(lines), preset, glow, bg, duration_frames, camera_zoom, canvas_width, canvas_height, camera_fn = std::move(camera_fn)](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = std::clamp(static_cast<f32>(ctx.frame) / static_cast<f32>(std::max<Frame>(duration_frames, 1)), 0.0f, 1.0f);
        if (camera_fn) {
            s.camera().set(camera_fn(p));
        } else {
            s.camera().set(camera_motion::dolly_in(p, camera_zoom));
        }

        s.layer("bg", [&](auto& l) { l.fill(bg); });
        s.layer("grid", [&](auto& l) {
            l.opacity(0.12f).grid_background("g", {.size={static_cast<f32>(canvas_width), static_cast<f32>(canvas_height)}, .grid_color={0.18f, 0.5f, 0.96f, 1.0f}, .spacing=96.0f});
        });

        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& l = lines[i];
            std::string revealed = l.reveal(ctx.frame);
            if (revealed.empty()) continue;

            auto obj = MotionObject::text("l" + std::to_string(i), std::move(revealed))
                .at(l.pos + l.sweep_val.offset(ctx.frame, l.start_frame))
                .font_size(l.font_size)
                .color(l.color_val)
                .tracking(l.tracking)
                .align(l.align_val)
                .vertical_align(VerticalAlign::Middle)
                .glow(glow)
                .time(0, duration_frames);

            if (preset == MotionPreset::PerspectiveSweepTextReveal) {
                presets::motion::perspective_sweep_text_reveal(obj);
            } else if (preset == MotionPreset::SoftDollyReveal) {
                presets::motion::soft_dolly_reveal(obj);
            } else if (preset == MotionPreset::DollyRotate2_5D) {
                presets::motion::dolly_rotate_2_5d(obj);
            } else if (preset == MotionPreset::StaggerReveal) {
                presets::motion::stagger_reveal(obj);
            } else if (preset == MotionPreset::FocusPull) {
                presets::motion::focus_pull(obj);
            } else {
                obj.preset(preset);
            }
            presets::motion::draw_motion_object(s, ctx, obj);
        }
        return s.build();
    });
}

} // namespace chronon3d::content::text::typewriter
