#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_presets.hpp>
#include <chronon3d/presets/motion_renderer.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include "text_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <utility>

namespace chronon3d::content::text {

namespace {

struct TypewriterLine {
    std::string text;
    Vec3 pos{0.0f, 0.0f, 0.0f};
    Vec2 box{W * 0.8f, 180.0f};
    f32 font_size{56.0f};
    Color color{1, 1, 1, 1};
    TextAlign align{TextAlign::Left};
    VerticalAlign vertical_align{VerticalAlign::Middle};
    f32 tracking{0.0f};
    f32 line_height{1.25f};
    f32 start_frame{0.0f};
    f32 chars_per_frame{2.0f};
    f32 fade_in_frames{10.0f};
    bool cursor{true};
    bool glow_enabled{false};

    struct SweepMotion {
        // Keep 2.5D off by default so flat typewriter scenes stay flat unless
        // a spec explicitly opts into depth motion.
        bool enabled{false};
        f32 amplitude_x{0.0f};
        f32 amplitude_y{0.0f};
        f32 amplitude_z{28.0f};
        f32 tilt_x_deg{0.0f};
        f32 tilt_y_deg{0.0f};
        f32 tilt_z_deg{0.0f};
        f32 scale_amount{0.0f};
        f32 period_frames{120.0f};
        f32 phase_frames{0.0f};
        f32 start_delay{0.0f};
        bool one_shot{false};
        f32 sweep_from{-1.0f};
        f32 sweep_to{1.0f};
        f32 sweep_duration_frames{90.0f};
    } sweep{};
};

struct TypewriterSceneSpec {
    std::string_view name;
    Color background{0.01f, 0.012f, 0.022f, 1.0f};
    Color panel_color{0.06f, 0.08f, 0.14f, 0.72f};
    Color grid_color{0.18f, 0.50f, 0.96f, 0.75f};
    f32 grid_opacity{0.12f};
    GridBackgroundParams grid{
        .size = {W, H},
        .bg_color = {0, 0, 0, 0},
        .grid_color = {0.18f, 0.50f, 0.96f, 1.0f},
        .spacing = 96.0f,
        .minor_thickness = 1.1f,
        .major_thickness = 1.1f,
        .major_every = 0,
        .centered = true,
    };
    Vec2 panel_size{W * 0.70f, 300.0f};
    Vec3 panel_pos{0.0f, 0.0f, 0.0f};
    f32 panel_radius{30.0f};
    f32 panel_opacity{0.82f};
    bool use_motion_renderer{false};
    std::vector<TypewriterLine> lines;
};

f32 clamp01(f32 v) {
    return std::clamp(v, 0.0f, 1.0f);
}

f32 line_opacity(Frame frame, const TypewriterLine& line) {
    const f32 t = static_cast<f32>(frame) - line.start_frame;
    if (t <= 0.0f) return 0.0f;
    return clamp01(t / std::max(line.fade_in_frames, 1.0f));
}

f32 sweep_wave(Frame frame, const TypewriterLine& line) {
    if (!line.sweep.enabled) return 0.0f;

    const f32 t = static_cast<f32>(frame) - line.start_frame - line.sweep.start_delay;
    if (line.sweep.one_shot) {
        if (t <= 0.0f) return line.sweep.sweep_from;
        const f32 duration = std::max(line.sweep.sweep_duration_frames, 1.0f);
        const f32 progress = clamp01(t / duration);
        return line.sweep.sweep_from + (line.sweep.sweep_to - line.sweep.sweep_from) * progress;
    }

    if (t <= 0.0f) return 0.0f;

    constexpr f32 kTwoPi = 6.28318530718f;
    const f32 period = std::max(line.sweep.period_frames, 1.0f);
    const f32 phase = (t / period) * kTwoPi + line.sweep.phase_frames;
    return std::sin(phase);
}

Vec3 sweep_offset(Frame frame, const TypewriterLine& line) {
    const f32 wave = sweep_wave(frame, line);
    return {
        line.sweep.amplitude_x * wave,
        line.sweep.amplitude_y * std::sin(wave * 1.7f),
        line.sweep.amplitude_z * wave
    };
}

Vec3 sweep_rotation(Frame frame, const TypewriterLine& line) {
    const f32 wave = sweep_wave(frame, line);
    return {
        line.sweep.tilt_x_deg * wave,
        line.sweep.tilt_y_deg * wave,
        line.sweep.tilt_z_deg * wave
    };
}

Vec3 sweep_scale(Frame frame, const TypewriterLine& line) {
    const f32 wave = std::abs(sweep_wave(frame, line));
    const f32 s = 1.0f + line.sweep.scale_amount * wave;
    return {s, s, 1.0f};
}

size_t visible_chars(Frame frame, const TypewriterLine& line) {
    const f32 t = static_cast<f32>(frame) - line.start_frame;
    if (t <= 0.0f) return 0;
    return std::min(line.text.size(), static_cast<size_t>(std::floor(t * std::max(line.chars_per_frame, 0.0f))));
}

std::string reveal_text(Frame frame, const TypewriterLine& line) {
    const size_t visible = visible_chars(frame, line);
    std::string out = line.text.substr(0, visible);
    if (line.cursor && visible < line.text.size()) {
        const bool blink_on = ((static_cast<int>(frame) / 5) % 2) == 0;
        if (blink_on) out.push_back('|');
    }
    return out;
}

void add_typewriter_background(SceneBuilder& s, const TypewriterSceneSpec& spec, f32 intro_opacity) {
    GridBackgroundParams grid = spec.grid;
    grid.grid_color = spec.grid_color;

    s.layer("bg", [&](LayerBuilder& l) {
        l.fill(spec.background);
    });

    s.layer("grid", [&](LayerBuilder& l) {
        l.opacity(intro_opacity * spec.grid_opacity);
        l.grid_background("grid_bg", grid);
    });

    if (spec.panel_opacity > 0.0f) {
        s.layer("panel", [&](LayerBuilder& l) {
            l.opacity(intro_opacity * spec.panel_opacity).pin_to(Anchor::Center);
            l.rounded_rect("panel_bg", {
                .size = spec.panel_size,
                .radius = spec.panel_radius,
                .color = spec.panel_color,
                .pos = spec.panel_pos,
                .fill = std::nullopt
            });
        });
    }
}

void add_typewriter_line(SceneBuilder& s, const TypewriterLine& line,
                         Frame frame, f32 scene_opacity, std::string layer_name) {
    const f32 opacity = scene_opacity * line_opacity(frame, line);
    if (opacity <= 0.0f) return;

    const std::string rendered = reveal_text(frame, line);
    if (rendered.empty()) return;
    const Vec3 sweep = sweep_offset(frame, line);
    const Vec3 motion_pos = {
        line.pos.x + sweep.x,
        line.pos.y + sweep.y,
        line.pos.z + sweep.z
    };
    const Vec3 motion_rot = sweep_rotation(frame, line);
    const bool needs_3d = line.sweep.enabled || std::abs(line.pos.z) > 1e-3f ||
                          std::abs(motion_rot.x) > 1e-3f || std::abs(motion_rot.y) > 1e-3f ||
                          std::abs(motion_rot.z) > 1e-3f;

    s.layer(std::move(layer_name), [&](LayerBuilder& l) {
        l.enable_3d(needs_3d)
            .opacity(opacity)
            .position(motion_pos)
            .rotate(motion_rot)
            .glow(line.glow_enabled ? 18.0f : 0.0f, line.glow_enabled ? 0.28f : 0.0f, {1.0f, 1.0f, 1.0f, 1.0f});
        l.text("text", {
            .text = rendered,
            .size = line.box,
            .pos = {0.0f, 0.0f, 0.0f},
            .font_path = "assets/fonts/Inter-Bold.ttf",
            .font_family = "Inter",
            .font_weight = 800,
            .font_style = "normal",
            .font_size = line.font_size,
            .color = line.color,
            .align = line.align,
            .vertical_align = line.vertical_align,
            .line_height = line.line_height,
            .tracking = line.tracking,
        });
    });
}

void add_typewriter_motion_line(SceneBuilder& s, const TypewriterLine& line, const FrameContext& frame_ctx,
                                f32 scene_opacity, std::string layer_name) {
    const f32 opacity = scene_opacity * line_opacity(frame_ctx.frame, line);
    if (opacity <= 0.0f) return;

    const std::string rendered = reveal_text(frame_ctx.frame, line);
    if (rendered.empty()) return;

    auto obj = chronon3d::presets::motion::MotionObject::text(std::move(layer_name), rendered)
        .at(line.pos)
        .size(line.box)
        .font_path("assets/fonts/Inter-Bold.ttf")
        .font_family("Inter")
        .font_weight(800)
        .font_size(line.font_size)
        .tracking(line.tracking)
        .align(line.align == TextAlign::Left
            ? chronon3d::presets::motion::TextAlign::Left
            : line.align == TextAlign::Right
                ? chronon3d::presets::motion::TextAlign::Right
                : chronon3d::presets::motion::TextAlign::Center)
        .vertical_align(line.vertical_align)
        .color(line.color)
        .opacity(opacity)
        .glow(line.glow_enabled)
        .time(0, 180);

    chronon3d::presets::motion::perspective_sweep_text_reveal(obj);

    chronon3d::presets::motion::draw_motion_object(s, frame_ctx, obj);
}

Composition make_typewriter_composition(TypewriterSceneSpec spec) {
    return composition({
        .name = std::string(spec.name),
        .width = 1920,
        .height = 1080,
        .duration = 180,
    }, [spec = std::move(spec)](const FrameContext& frame_ctx) {
        SceneBuilder s(frame_ctx);
        const f32 intro_opacity = clamp01(static_cast<f32>(frame_ctx.frame) / 12.0f);
        const f32 p = clamp01(static_cast<f32>(frame_ctx.frame) / 180.0f);

        s.camera().set(chronon3d::camera_motion::dramatic_push(p, 1380.0f));

        add_typewriter_background(s, spec, intro_opacity);

        for (size_t i = 0; i < spec.lines.size(); ++i) {
            if (spec.use_motion_renderer) {
                add_typewriter_motion_line(s, spec.lines[i], frame_ctx, intro_opacity, "line_" + std::to_string(i));
            } else {
                add_typewriter_line(
                    s,
                    spec.lines[i],
                    frame_ctx.frame,
                    intro_opacity,
                    "line_" + std::to_string(i)
                );
            }
        }

        return s.build();
    });
}

TypewriterSceneSpec classic_spec() {
    return {
        .name = "TextTypewriter",
        .background = {0.010f, 0.012f, 0.022f, 1.0f},
        .panel_color = {0.05f, 0.07f, 0.14f, 0.76f},
        .grid_color = {0.18f, 0.50f, 0.96f, 0.75f},
        .grid_opacity = 0.12f,
        .panel_size = {W * 0.70f, 320.0f},
        .panel_pos = {0.0f, 0.0f, 0.0f},
        .panel_radius = 30.0f,
        .panel_opacity = 0.0f,
        .lines = {
            {
                .text = "THE ENGINE LEARNED TO SPEAK.",
                .pos = {0.0f, -34.0f, 0.0f},
                .box = {W * 0.62f, 120.0f},
                .font_size = 64.0f,
                .color = {0.25f, 0.58f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 6.0f,
                .line_height = 1.0f,
                .start_frame = 0.0f,
                .chars_per_frame = 1.8f,
                .fade_in_frames = 10.0f,
            },
            {
                .text = "Typed frame by frame, the story becomes motion.",
                .pos = {0.0f, 54.0f, 0.0f},
                .box = {W * 0.66f, 120.0f},
                .font_size = 28.0f,
                .color = {0.90f, 0.92f, 0.98f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 0.5f,
                .line_height = 1.25f,
                .start_frame = 36.0f,
                .chars_per_frame = 2.8f,
                .fade_in_frames = 8.0f,
            }
        }
    };
}

TypewriterSceneSpec terminal_spec() {
    return {
        .name = "TextTypewriterTerminal",
        .background = {0.006f, 0.010f, 0.016f, 1.0f},
        .panel_color = {0.03f, 0.05f, 0.10f, 0.84f},
        .grid_color = {0.24f, 0.62f, 1.0f, 0.65f},
        .grid_opacity = 0.08f,
        .panel_size = {W * 0.78f, 420.0f},
        .panel_radius = 24.0f,
        .panel_opacity = 0.0f,
        .use_motion_renderer = true,
        .lines = {
            {
                .text = "TYPEWRITER / PERSPECTIVE SWEEP",
                .pos = {0.0f, 0.0f, 0.0f},
                .box = {W * 0.74f, 150.0f},
                .font_size = 62.0f,
                .color = {0.88f, 0.96f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 6.0f,
                .line_height = 1.0f,
                .start_frame = 0.0f,
                .chars_per_frame = 2.0f,
                .fade_in_frames = 18.0f,
                .cursor = false,
                .glow_enabled = true,
            }
        }
    };
}

TypewriterSceneSpec quote_spec() {
    return {
        .name = "TextTypewriterQuote",
        .background = {0.008f, 0.011f, 0.024f, 1.0f},
        .panel_color = {0.05f, 0.06f, 0.12f, 0.70f},
        .grid_color = {0.18f, 0.50f, 0.96f, 0.70f},
        .grid_opacity = 0.10f,
        .panel_size = {W * 0.74f, 300.0f},
        .panel_radius = 34.0f,
        .panel_opacity = 0.78f,
        .lines = {
            {
                .text = "WE WRITE THE WORDS",
                .pos = {0.0f, -56.0f, 0.0f},
                .box = {W * 0.68f, 110.0f},
                .font_size = 60.0f,
                .color = {0.25f, 0.58f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 8.0f,
                .chars_per_frame = 1.7f,
            },
            {
                .text = "THEN THE TEXT WRITES THE SCENE.",
                .pos = {0.0f, 40.0f, 0.0f},
                .box = {W * 0.72f, 110.0f},
                .font_size = 42.0f,
                .color = {0.92f, 0.94f, 0.98f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 3.0f,
                .start_frame = 28.0f,
                .chars_per_frame = 2.4f,
            }
        }
    };
}

TypewriterSceneSpec manifest_spec() {
    return {
        .name = "TextTypewriterManifest",
        .background = {0.012f, 0.010f, 0.018f, 1.0f},
        .panel_color = {0.06f, 0.08f, 0.13f, 0.80f},
        .grid_color = {0.18f, 0.50f, 0.96f, 0.65f},
        .grid_opacity = 0.08f,
        .panel_size = {W * 0.80f, 470.0f},
        .panel_radius = 28.0f,
        .panel_opacity = 0.80f,
        .lines = {
            {
                .text = "MANIFEST",
                .pos = {-W * 0.22f, -138.0f, 0.0f},
                .box = {W * 0.22f, 80.0f},
                .font_size = 26.0f,
                .color = {0.35f, 1.0f, 0.62f, 1.0f},
                .align = TextAlign::Left,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 5.0f,
                .chars_per_frame = 4.0f,
            },
            {
                .text = "every frame can become a sentence,",
                .pos = {0.0f, -38.0f, 0.0f},
                .box = {W * 0.68f, 110.0f},
                .font_size = 42.0f,
                .color = {0.95f, 0.96f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 1.0f,
                .chars_per_frame = 2.6f,
            },
            {
                .text = "and every sentence can be typed into motion.",
                .pos = {0.0f, 60.0f, 0.0f},
                .box = {W * 0.74f, 110.0f},
                .font_size = 30.0f,
                .color = {0.78f, 0.82f, 0.90f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 0.6f,
                .start_frame = 26.0f,
                .chars_per_frame = 2.9f,
            },
            {
                .text = "typewriter / scalable / data-driven",
                .pos = {0.0f, 156.0f, 0.0f},
                .box = {W * 0.68f, 80.0f},
                .font_size = 24.0f,
                .color = {0.35f, 1.0f, 0.62f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 4.0f,
                .start_frame = 54.0f,
                .chars_per_frame = 3.5f,
            }
        }
    };
}

TypewriterSceneSpec chapter_spec() {
    return {
        .name = "TextTypewriterChapter",
        .background = {0.009f, 0.012f, 0.020f, 1.0f},
        .panel_color = {0.04f, 0.06f, 0.11f, 0.76f},
        .grid_color = {0.18f, 0.50f, 0.96f, 0.72f},
        .grid_opacity = 0.10f,
        .panel_size = {W * 0.76f, 360.0f},
        .panel_radius = 26.0f,
        .panel_opacity = 0.82f,
        .lines = {
            {
                .text = "CHAPTER 01",
                .pos = {0.0f, -98.0f, 0.0f},
                .box = {W * 0.52f, 90.0f},
                .font_size = 34.0f,
                .color = {0.35f, 1.0f, 0.62f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 10.0f,
                .chars_per_frame = 3.0f,
            },
            {
                .text = "THE FIRST WORD ARRIVES",
                .pos = {0.0f, -12.0f, 0.0f},
                .box = {W * 0.72f, 120.0f},
                .font_size = 54.0f,
                .color = {0.92f, 0.96f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 5.0f,
                .chars_per_frame = 1.9f,
            },
            {
                .text = "typing makes timing visible.",
                .pos = {0.0f, 88.0f, 0.0f},
                .box = {W * 0.52f, 90.0f},
                .font_size = 26.0f,
                .color = {0.78f, 0.82f, 0.90f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 1.2f,
                .start_frame = 34.0f,
                .chars_per_frame = 3.0f,
            }
        }
    };
}

} // namespace

Composition text_typewriter() {
    return make_typewriter_composition(classic_spec());
}

Composition text_typewriter_terminal() {
    return make_typewriter_composition(terminal_spec());
}

Composition text_typewriter_quote() {
    return make_typewriter_composition(quote_spec());
}

Composition text_typewriter_manifest() {
    return make_typewriter_composition(manifest_spec());
}

Composition text_typewriter_chapter() {
    return make_typewriter_composition(chapter_spec());
}

} // namespace chronon3d::content::text
