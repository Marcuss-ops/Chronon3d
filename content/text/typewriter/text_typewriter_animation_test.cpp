#include "typewriter_common.hpp"

#include <array>

namespace chronon3d::content::text {

namespace {

using typewriter::TypewriterLine;
using typewriter::make_typewriter;

struct SegmentSpec {
    const char* name;
    presets::motion::MotionPreset preset;
    std::vector<TypewriterLine> lines;
    Color bg;
    bool glow{false};
    std::function<Camera2_5D(f32)> camera_fn{};
};

Composition build_segment(const SegmentSpec& spec) {
    return make_typewriter(
        spec.name,
        spec.lines,
        spec.preset,
        spec.glow,
        spec.bg,
        30,
        1380.0f,
        1280,
        720,
        spec.camera_fn
    );
}

} // namespace

Composition text_typewriter_animation_test() {
    const std::array<SegmentSpec, 6> segments = {{
        {
            "PerspectiveSweep",
            presets::motion::MotionPreset::PerspectiveSweepTextReveal,
            {
                TypewriterLine("PERSPECTIVE SWEEP")
                    .set_pos({0, 0, 0})
                    .set_font(62, 6)
                    .set_timing(0, 2.6f)
                    .set_color({0.88f, 0.96f, 1.0f, 1.0f}),
            },
            {0.006f, 0.010f, 0.016f, 1.0f},
            true,
            [](f32 p) { return camera_motion::parallax_sweep(p, 120.0f); }
        },
        {
            "SoftDolly",
            presets::motion::MotionPreset::SoftDollyReveal,
            {
                TypewriterLine("SOFT DOLLY REVEAL")
                    .set_pos({0, 0, 0})
                    .set_font(60, 6)
                    .set_timing(0, 2.4f)
                    .set_color({0.25f, 0.58f, 1.0f, 1.0f}),
            },
            {0.015f, 0.015f, 0.025f, 1.0f},
            false,
            [](f32 p) { return camera_motion::dolly_in(p); }
        },
        {
            "DollyRotate",
            presets::motion::MotionPreset::DollyRotate2_5D,
            {
                TypewriterLine("DOLLY / 2.5D ROTATE")
                    .set_pos({0, -28, 0})
                    .set_font(62, 6)
                    .set_timing(0, 2.0f)
                    .set_color({0.28f, 0.62f, 1.0f, 1.0f}),
                TypewriterLine("the camera moves and turns with it.")
                    .set_pos({0, 56, 0})
                    .set_font(28, 1.0f)
                    .set_timing(28, 2.8f)
                    .set_color({0.88f, 0.92f, 0.98f, 1.0f}),
            },
            {0.010f, 0.012f, 0.020f, 1.0f},
            true,
            [](f32 p) { return camera_motion::dolly_rotate(p, 1000.0f); }
        },
        {
            "Stagger",
            presets::motion::MotionPreset::StaggerReveal,
            {
                TypewriterLine("STAGGERED REVEAL")
                    .set_pos({0, 0, 0})
                    .set_font(58, 7)
                    .set_timing(0, 2.2f)
                    .set_color({1.0f, 0.65f, 0.2f, 1.0f}),
            },
            {0.012f, 0.015f, 0.022f, 1.0f},
            false,
            [](f32 p) { return camera_motion::orbit_small(p); }
        },
        {
            "FocusPull",
            presets::motion::MotionPreset::FocusPull,
            {
                TypewriterLine("FOCUS PULL")
                    .set_pos({0, 0, 0})
                    .set_font(62, 6)
                    .set_timing(0, 2.2f)
                    .set_color({0.98f, 0.96f, 0.92f, 1.0f}),
            },
            {0.010f, 0.014f, 0.022f, 1.0f},
            false,
            [](f32 p) { return camera_motion::dramatic_push(p); }
        },
        {
            "GlowBloom",
            presets::motion::MotionPreset::GlowBloom,
            {
                TypewriterLine("GLOW BLOOM")
                    .set_pos({0, 0, 0})
                    .set_font(60, 6)
                    .set_timing(0, 2.4f)
                    .set_color({0.96f, 0.97f, 1.0f, 1.0f}),
            },
            {0.008f, 0.010f, 0.020f, 1.0f},
            true,
            [](f32 p) { return camera_motion::roll_reveal(p, 8.0f); }
        }
    }};

    return composition({.name = "TextTypewriterAnimationTest", .width = 1280, .height = 720, .duration = 180}, [segments](const FrameContext& ctx) {
        const i32 segment_idx = std::clamp<i32>(static_cast<i32>(ctx.frame / 30), 0, static_cast<i32>(segments.size()) - 1);
        const i32 local_frame = static_cast<i32>(ctx.frame % 30);
        auto segment = build_segment(segments[static_cast<size_t>(segment_idx)]);
        return segment.evaluate(local_frame, ctx.resource);
    });
}

} // namespace chronon3d::content::text
