#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

// AnimatedTitleCard — 3-second motion-graphics title sequence (90 frames @ 30fps)
//
// Timeline:
//   0–89  background always visible
//   0–~30 title block springs in from above
//  10–50  accent line grows left-to-right (InOutCubic)
//   5–35  decorative circles fade in
//  20–45  subtitle strip fades in via sequence + held_progress
//  60–89  global fade-out
//
// Demonstrates: spring, interpolate, sequence, held_progress,
//               rect, circle, line, alpha blending, draw order.

static Composition AnimatedTitleCard() {
    CompositionSpec spec{
        .name = "AnimatedTitleCard",
        .width = 800,
        .height = 450,
        .duration = 90
    };

    return Composition{
        spec,
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);

            // Background — dark blue-gray
            s.rect("bg", {400.0f, 225.0f, 0.0f},
                Color(0.08f, 0.08f, 0.12f, 1.0f), {800.0f, 450.0f});

            // Global fade-out begins at frame 60
            f32 fade = interpolate(ctx.frame, 60, 90, 1.0f, 0.0f);

            // Title block — springs down from above; settles at y=225 by ~frame 30
            f32 box_y = spring(ctx, -60.0f, 225.0f);
            s.rect("title-block", {400.0f, box_y, 0.0f},
                Color(0.93f, 0.93f, 0.97f, fade), {420.0f, 80.0f});

            // Accent line — eased growth left→right between frames 10 and 50
            f32 line_x = interpolate(ctx.frame, 10, 50, 190.0f, 610.0f, Easing::InOutCubic);
            s.line("accent-line",
                {190.0f, 280.0f, 0.0f},
                {line_x,  280.0f, 0.0f},
                Color(0.38f, 0.68f, 1.0f, fade));

            // Subtitle strip — fades in over 25 frames, then holds via held_progress
            auto sub = sequence(ctx, Frame{20}, Frame{25});
            s.rect("subtitle-strip", {400.0f, 315.0f, 0.0f},
                Color(0.45f, 0.45f, 0.58f, sub.held_progress() * fade), {280.0f, 18.0f});

            // Decorative circles — fade in between frames 5 and 35
            f32 c_alpha = interpolate(ctx.frame, 5, 35, 0.0f, 1.0f) * fade;
            s.circle("deco-left",   {150.0f, 225.0f, 0.0f}, 22.0f,
                Color(0.38f, 0.68f, 1.0f, c_alpha * 0.55f));
            s.circle("deco-right",  {650.0f, 225.0f, 0.0f}, 22.0f,
                Color(0.38f, 0.68f, 1.0f, c_alpha * 0.55f));
            s.circle("deco-accent", {400.0f, 148.0f, 0.0f}, 10.0f,
                Color(1.0f, 0.76f, 0.20f, c_alpha));

            return s.build();
        }
    };
}

CHRONON_REGISTER_COMPOSITION("AnimatedTitleCard", AnimatedTitleCard)
