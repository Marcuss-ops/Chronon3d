// ── AppleStyleHero — clean, minimal Apple-style hero section ────────────
// Included inline inside namespace chronon3d::scene_presets.

inline Composition apple_style_hero() {
    return composition({
        .name = "AppleStyleHero",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 100,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.animated_camera(camera_rig::parallax_pan({
            .from_position = {-20.0f, 0.0f, -1000.0f},
            .to_position   = { 20.0f, 0.0f, -1000.0f},
            .target = {0.0f, 0.0f, 0.0f},
            .zoom = 1000.0f,
            .duration = 100,
            .start_frame = Frame{0},
            .easing = EasingCurve{Easing::InOutSine},
        }));

        s.screen_layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {
                .size = {W, H},
                .color = {0.99f, 0.99f, 1.0f, 1.0f},
                .fill = detail::linear_gradient_v(
                    {1.0f, 1.0f, 1.0f, 1.0f},
                    {0.97f, 0.98f, 1.0f, 1.0f}
                ),
            });
        });

        s.layer("accent_1", [](LayerBuilder& l) {
            l.opacity(0.06f);
            l.rounded_rect("shape", {
                .size = {400.0f, 400.0f}, .radius = 200.0f,
                .color = {0.3f, 0.5f, 1.0f, 1.0f},
                .pos = {750.0f, -300.0f, 0.0f},
            });
        });
        s.layer("accent_2", [](LayerBuilder& l) {
            l.opacity(0.04f);
            l.rounded_rect("shape", {
                .size = {250.0f, 250.0f}, .radius = 125.0f,
                .color = {0.6f, 0.3f, 1.0f, 1.0f},
                .pos = {-700.0f, 350.0f, 0.0f},
            });
        });

        TextAnimator hero_title;
        hero_title.text("Think Different.")
            .font_size(88.0f).font_path(detail::default_font())
            .font_weight(800).color({0.05f, 0.05f, 0.08f, 1.0f})
            .align(TextAlign::Center)
            .config({.mode = TextAnimMode::ByWord, .duration = Frame{25},
                     .delay_per_unit = Frame{10}, .easing = Easing::OutCubic,
                     .animate_opacity = true, .animate_slide = true,
                     .slide_from = {0.0f, 30.0f, 0.0f},
                     .animate_scale = true, .scale_from = {0.92f, 0.92f, 1.0f}});
        hero_title.build(s, "hero");

        s.layer("subtitle", [](LayerBuilder& l) {
            l.position({0.0f, 105.0f, 0.0f});
            l.text("sub", detail::text_preset(
                "The most powerful creative tool. Ever.",
                26.0f, 400, {0.45f, 0.45f, 0.50f, 1.0f},
                TextAlign::Center, {700.0f, 50.0f}
            ));
            l.slide_in({0.0f, 25.0f, 0.0f}, Frame{35}, EasingCurve{Easing::OutCubic});
        });

        s.layer("cta_primary", [](LayerBuilder& l) {
            l.position({-90.0f, 220.0f, 0.0f});
            l.rounded_rect("pill", {
                .size = {150.0f, 50.0f}, .radius = 25.0f,
                .color = {0.05f, 0.05f, 0.08f, 1.0f},
            });
            l.text("label", detail::text_preset(
                "Learn More", 16.0f, 600, {1.0f, 1.0f, 1.0f, 1.0f},
                TextAlign::Center, {150.0f, 50.0f}
            ));
            l.soft_pop(Frame{30});
        });

        s.layer("cta_secondary", [](LayerBuilder& l) {
            l.position({90.0f, 220.0f, 0.0f});
            l.rounded_rect("outline", {
                .size = {130.0f, 50.0f}, .radius = 25.0f,
                .color = {1.0f, 1.0f, 1.0f, 1.0f},
            });
            l.text("label", detail::text_preset(
                "Watch the film", 15.0f, 500, {0.05f, 0.05f, 0.08f, 1.0f},
                TextAlign::Center, {130.0f, 50.0f}
            ));
            l.settle(0.05f, Frame{25});
        });

        return s.build();
    });
}
