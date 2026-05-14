#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

// AnimatedTitleCard — 3-second motion-graphics title sequence (90 frames @ 30fps)
//
// Timeline:
//   0–89  background always visible
//   0–~30 title block springs in from above  (rounded_rect + drop shadow)
//  10–50  accent line grows left→right       (InOutCubic)
//   5–35  decorative circles fade in          (glow)
//  20–45  subtitle strip fades in             (sequence + held_progress)
//  60–89  global fade-out
Composition AnimatedTitleCard() {
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

            // Background
            s.rect("bg", {
                .size = {800.0f, 450.0f},
                .color = Color(0.08f, 0.08f, 0.12f, 1.0f),
                .pos = {400.0f, 225.0f, 0.0f}
            });

            // Global fade-out begins at frame 60
            const f32 fade = interpolate(ctx.frame, 60, 90, 1.0f, 0.0f);

            // Title block — springs down from above, settles at y=225
            const f32 box_y = spring(ctx, -60.0f, 225.0f);
            s.rounded_rect("title-block", {
                    .size = {420.0f, 80.0f},
                    .radius = 14.0f,
                    .color = Color(0.93f, 0.93f, 0.97f, fade),
                    .pos = {400.0f, box_y, 0.0f}
                })
             .with_shadow(DropShadow{
                    .enabled = true,
                    .offset  = {0.0f, 10.0f},
                    .color   = {0.0f, 0.0f, 0.0f, 0.45f * fade},
                    .radius  = 18.0f});

            s.text("title-text", {
                   .content = "CHRONON",
                   .style = {
                       .font_path = "assets/fonts/Inter-Bold.ttf",
                       .size = 60.0f,
                       .color = Color(0.08f, 0.08f, 0.12f, fade)
                   },
                   .pos = {400.0f, box_y, 0.0f} // Center aligned to the box center
                });

            // Accent line — eased growth left→right
            const f32 line_x = interpolate(ctx.frame, 10, 50, 190.0f, 610.0f, Easing::InOutCubic);
            s.line("accent-line", {
                .from = {190.0f, 280.0f, 0.0f},
                .to = {line_x, 280.0f, 0.0f},
                .color = Color(0.38f, 0.68f, 1.0f, fade)
            });

            // Subtitle text — fades in
            const auto sub = sequence(ctx, Frame{20}, Frame{25});
            s.text("subtitle-text", {
                   .content = "MOTION GRAPHICS ENGINE",
                   .style = {
                       .font_path = "assets/fonts/Inter-Regular.ttf",
                       .size = 22.0f,
                       .color = Color(0.75f, 0.80f, 1.0f, sub.held_progress() * fade)
                   },
                   .pos = {400.0f, 315.0f, 0.0f}
                });

            // Decorative circles — fade in with glow
            const f32 c_alpha = interpolate(ctx.frame, 5, 35, 0.0f, 1.0f) * fade;
            s.circle("deco-left", {
                    .radius = 22.0f,
                    .color = Color(0.38f, 0.68f, 1.0f, c_alpha * 0.55f),
                    .pos = {150.0f, 225.0f, 0.0f}
                })
             .with_glow(Glow{
                    .enabled   = true,
                    .radius    = 18.0f,
                    .intensity = 0.65f * c_alpha,
                    .color     = {0.38f, 0.68f, 1.0f, 1.0f}});

            s.circle("deco-right", {
                    .radius = 22.0f,
                    .color = Color(0.38f, 0.68f, 1.0f, c_alpha * 0.55f),
                    .pos = {650.0f, 225.0f, 0.0f}
                })
             .with_glow(Glow{
                    .enabled   = true,
                    .radius    = 18.0f,
                    .intensity = 0.65f * c_alpha,
                    .color     = {0.38f, 0.68f, 1.0f, 1.0f}});

            s.circle("deco-accent", {
                    .radius = 10.0f,
                    .color = Color(1.0f, 0.76f, 0.20f, c_alpha),
                    .pos = {400.0f, 148.0f, 0.0f}
                })
             .with_glow(Glow{
                    .enabled   = true,
                    .radius    = 14.0f,
                    .intensity = 0.8f * c_alpha,
                    .color     = {1.0f, 0.76f, 0.20f, 1.0f}});

            return s.build();
        }
    };
}

CHRONON_REGISTER_COMPOSITION("AnimatedTitleCard", AnimatedTitleCard)
