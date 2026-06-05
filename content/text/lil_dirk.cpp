#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_presets.hpp>
#include <chronon3d/presets/motion_renderer.hpp>
#include <string>

namespace chronon3d::content::text {

Composition lil_dirk() {
    return composition({
        .name     = "LilDirk",
        .width    = 1920,
        .height   = 1080,
        .duration = 180,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 W = static_cast<f32>(ctx.width);
        const f32 H = static_cast<f32>(ctx.height);
        const f32 half_w = W * 0.5f;
        const f32 half_h = H * 0.5f;

        // Configure static 2.5D camera so 3D projections on layers work
        s.camera().enable()
                  .position({0.0f, 0.0f, -1000.0f})
                  .zoom(1000.0f)
                  .point_of_interest({0.0f, 0.0f, 0.0f});

        // ── Dark background ───────────────────────────────────────────────
        s.layer("bg", [W, H](auto& l) {
            l.rect("fill", {
                .size  = {W, H},
                .color = Color{0.058f, 0.058f, 0.065f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f},
            });
        });

        // ── Grid — minor lines ────────────────────────────────────────────
        const f32 spacing    = 80.0f;
        const f32 major_step = spacing * 4.0f;
        const Color minor_col{1.0f, 1.0f, 1.0f, 0.08f};
        const Color major_col{1.0f, 1.0f, 1.0f, 0.20f};

        s.layer("grid_minor", [half_w, half_h, spacing, minor_col](auto& l) {
            int idx = 0;
            for (f32 x = -half_w; x <= half_w; x += spacing) {
                l.line("vx" + std::to_string(idx++), {
                    .from = {x, -half_h, 0.0f}, .to = {x, half_h, 0.0f},
                    .thickness = 1.5f, .color = minor_col
                });
            }
            for (f32 y = -half_h; y <= half_h; y += spacing) {
                l.line("hy" + std::to_string(idx++), {
                    .from = {-half_w, y, 0.0f}, .to = {half_w, y, 0.0f},
                    .thickness = 1.5f, .color = minor_col
                });
            }
        });

        // ── Grid — major lines ────────────────────────────────────────────
        s.layer("grid_major", [half_w, half_h, major_step, major_col](auto& l) {
            int idx = 0;
            for (f32 x = -half_w; x <= half_w; x += major_step) {
                l.line("Vx" + std::to_string(idx++), {
                    .from = {x, -half_h, 0.0f}, .to = {x, half_h, 0.0f},
                    .thickness = 3.0f, .color = major_col
                });
            }
            for (f32 y = -half_h; y <= half_h; y += major_step) {
                l.line("Hy" + std::to_string(idx++), {
                    .from = {-half_w, y, 0.0f}, .to = {half_w, y, 0.0f},
                    .thickness = 3.0f, .color = major_col
                });
            }
        });

        // ── Soft spotlight from above ─────────────────────────────────────
        s.layer("spotlight", [W, H](auto& l) {
            l.circle("spot", {
                .radius = W * 0.40f,
                .color  = Color{1.0f, 1.0f, 1.0f, 0.028f},
                .pos    = {0.0f, -H * 0.22f, 0.0f},
            }).blur(W * 0.14f);
        });

        // ── LIL DIRK title (Animated) ─────────────────────────────────────
        s.layer("title", [&ctx](auto& l) {
            l.from(0).duration(180);
            l.enable_3d();

            l.position_anim()
             .key(0, Vec3{0.0f, 0.0f, 0.0f}, Easing::InOutCubic)
             .key(179, Vec3{0.0f, 0.0f, 0.0f});

            l.rotate_anim()
             .key(0, Vec3{-22.0f, 48.0f, -7.0f}, Easing::InOutCubic)
             .key(179, Vec3{22.0f, -48.0f, 7.0f});

            l.text("t", TextParams{
                .text       = "LIL DIRK",
                .size       = {700.0f, 140.0f},
                .pos        = {0.0f, -30.0f, 0.0f},
                .font_path  = "assets/fonts/Inter-Bold.ttf",
                .font_size  = 118.0f,
                .color      = Color{1.0f, 1.0f, 1.0f, 1.0f},
                .align      = TextAlign::Center,
            }).with_glow(Glow{
                .enabled   = true,
                .radius    = 18.0f,
                .intensity = 0.68f,
                .color     = Color{1.0f, 1.0f, 1.0f, 1.0f},
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::text

// Registration handled by TextModule in text_module.cpp
