// ── SaaSIntro — modern SaaS product hero (extracted from scene_presets.hpp) ──
// Included inline inside namespace chronon3d::scene_presets.

inline Composition saas_intro() {
    return composition({
        .name = "SaaSIntro",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 120,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Camera: push-in
        s.animated_camera(camera_rig::hero_push_in({
            .from_position = {0.0f, 0.0f, -1100.0f},
            .to_position   = {0.0f, 0.0f, -750.0f},
            .from_tilt = -3.0f,
            .to_tilt   = 1.0f,
            .from_yaw  = 0.0f,
            .to_yaw    = 2.0f,
            .zoom = 1000.0f,
            .duration = 100,
            .start_frame = Frame{0},
            .easing = EasingCurve::cubic_bezier(0.16f, 1.0f, 0.3f, 1.0f),
        }));

        // Background
        s.screen_layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {
                .size = {W, H},
                .color = {0.02f, 0.02f, 0.05f, 1.0f},
                .fill = detail::linear_gradient_v(
                    {0.02f, 0.02f, 0.08f, 1.0f},
                    {0.01f, 0.01f, 0.03f, 1.0f}
                ),
            });
        });

        s.screen_layer("bg_glow", [](LayerBuilder& l) {
            l.blend(BlendMode::Add);
            l.opacity(0.30f);
            l.circle("glow", {
                .radius = 500.0f,
                .color = {0.15f, 0.40f, 1.0f, 0.20f},
                .pos = {0.0f, -120.0f, 0.0f},
            });
        });

        // Accents
        s.layer("accent_top", [](LayerBuilder& l) {
            l.opacity(0.12f);
            l.blend(BlendMode::Add);
            l.rounded_rect("bar", {
                .size = {3.0f, 300.0f},
                .radius = 1.5f,
                .color = {0.3f, 0.6f, 1.0f, 1.0f},
                .pos = {-640.0f, -280.0f, 0.0f},
            });
        });

        s.layer("accent_bottom", [](LayerBuilder& l) {
            l.opacity(0.08f);
            l.blend(BlendMode::Add);
            l.rounded_rect("bar", {
                .size = {600.0f, 2.0f},
                .radius = 1.0f,
                .color = {0.4f, 0.7f, 1.0f, 1.0f},
                .pos = {100.0f, 320.0f, 0.0f},
            });
        });

        // Title
        TextAnimator title_anim;
        title_anim
            .text("Build Faster")
            .font_size(96.0f)
            .font_path(detail::default_font())
            .font_weight(900)
            .color({1.0f, 1.0f, 1.0f, 1.0f})
            .align(TextAlign::Center)
            .config({
                .mode = TextAnimMode::ByCharacter,
                .duration = Frame{25},
                .delay_per_unit = Frame{3},
                .easing = Easing::OutCubic,
                .animate_opacity = true,
                .animate_slide = true,
                .slide_from = {0.0f, 50.0f, 0.0f},
                .animate_scale = true,
                .scale_from = {0.85f, 0.85f, 1.0f},
            });
        title_anim.build(s, "title");

        // Subtitle
        s.layer("subtitle", [](LayerBuilder& l) {
            l.position({0.0f, 110.0f, 0.0f});
            l.text("sub", detail::text_preset(
                "The modern SaaS platform for teams",
                32.0f, 400, {0.55f, 0.60f, 0.70f, 1.0f},
                TextAlign::Center, {800.0f, 60.0f}
            ));
            l.slide_in({0.0f, 30.0f, 0.0f}, Frame{30}, EasingCurve{Easing::OutCubic});
            l.opacity(0.55f);
        });

        // Feature cards
        struct CardInfo { std::string label; };
        const CardInfo cards[] = {{"Lightning Fast"}, {"Secure by Default"}, {"Global Scale"}};

        for (int i = 0; i < 3; ++i) {
            f32 card_x = (static_cast<f32>(i) - 1.0f) * 340.0f;
            s.layer("card_" + std::to_string(i), [&, i, card_x](LayerBuilder& l) {
                l.position({card_x, 260.0f, 0.0f});
                l.rounded_rect("bg", {
                    .size = {280.0f, 180.0f},
                    .radius = 16.0f,
                    .color = {0.06f, 0.06f, 0.12f, 0.90f},
                    .fill = detail::linear_gradient_v(
                        {0.08f, 0.08f, 0.16f, 0.85f},
                        {0.04f, 0.04f, 0.10f, 0.90f}
                    ),
                });
                l.circle("icon", {
                    .radius = 20.0f,
                    .color = {0.2f, 0.5f, 1.0f, 0.25f},
                    .pos = {0.0f, -40.0f, 0.0f},
                });
                l.text("label", detail::text_preset(
                    cards[i].label, 20.0f, 600, {0.85f, 0.87f, 0.92f, 1.0f},
                    TextAlign::Center, {260.0f, 40.0f}, {0.0f, 20.0f, 0.0f}
                ));
                l.soft_pop(Frame{35});
            });
        }

        // CTA
        s.layer("cta", [](LayerBuilder& l) {
            l.position({0.0f, 430.0f, 0.0f});
            l.drop_shadow({0.0f, 12.0f}, {0.2f, 0.5f, 1.0f, 0.25f}, 24.0f);
            l.rounded_rect("pill", {
                .size = {240.0f, 56.0f},
                .radius = 28.0f,
                .color = {0.2f, 0.5f, 1.0f, 1.0f},
                .fill = detail::linear_gradient_h(
                    {0.15f, 0.45f, 1.0f, 1.0f},
                    {0.35f, 0.60f, 1.0f, 1.0f}
                ),
            });
            l.text("get", detail::text_preset(
                "Get Started", 18.0f, 700, {1.0f, 1.0f, 1.0f, 1.0f},
                TextAlign::Center, {240.0f, 56.0f}
            ));
            l.settle(0.06f, Frame{25});
        });

        return s.build();
    });
}
