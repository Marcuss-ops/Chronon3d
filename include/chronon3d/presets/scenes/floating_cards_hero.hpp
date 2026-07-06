// ── FloatingCardsHero — cards floating in 3D space with parallax ───────
// Included inline inside namespace chronon3d::scene_presets.

inline Composition floating_cards_hero() {
    return composition({
        .name = "FloatingCardsHero",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 150,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.animated_camera(camera_rig::subtle_float({
            .base_position = {0.0f, 0.0f, -1000.0f},
            .x_amplitude = 12.0f,
            .y_amplitude = 6.0f,
            .z_amplitude = 15.0f,
            .x_frequency = 0.3f,
            .y_frequency = 0.2f,
            .z_frequency = 0.15f,
            .zoom = 1000.0f,
            .duration = 150,
        }));

        s.grid_background("grid", {
            .size = {W, H},
            .bg_color = {0.02f, 0.02f, 0.04f, 1.0f},
            .grid_color = {0.30f, 0.55f, 1.0f, 0.04f},
            .spacing = 120.0f,
            .minor_thickness = 1.0f,
            .major_thickness = 2.5f,
            .major_every = 4,
        });

        struct CardSpec {
            std::string name; Vec3 position; std::string title;
            Color accent; f32 depth_z;
        };
        const CardSpec specs[] = {
            {"card_0", {-320.0f, -60.0f, -60.0f}, "Analytics",    {0.2f, 0.6f, 1.0f, 1.0f}, 180.0f},
            {"card_1", { 320.0f, -40.0f, -20.0f}, "Automation",   {0.8f, 0.3f, 1.0f, 1.0f}, 220.0f},
            {"card_2", {-280.0f,  80.0f,  30.0f}, "Security",     {0.1f, 0.9f, 0.5f, 1.0f}, 260.0f},
            {"card_3", { 280.0f,  70.0f,  10.0f}, "Integrations", {1.0f, 0.5f, 0.2f, 1.0f}, 200.0f},
        };

        for (const auto& sp : specs) {
            s.layer(sp.name, [&sp](LayerBuilder& l) {
                l.position(sp.position);
                l.enable_3d(true);
                l.rounded_rect("bg", {
                    .size = {240.0f, 160.0f},
                    .radius = 14.0f,
                    .color = {0.06f, 0.06f, 0.12f, 0.88f},
                    .fill = detail::linear_gradient_v(
                        {0.09f, 0.09f, 0.18f, 0.85f},
                        {0.04f, 0.04f, 0.10f, 0.92f}
                    ),
                });
                l.rounded_rect("accent", {
                    .size = {240.0f, 3.0f}, .radius = 0.0f,
                    .color = sp.accent, .pos = {0.0f, -78.5f, 0.0f},
                });
                l.circle("icon", {
                    .radius = 18.0f,
                    .color = Color{sp.accent.r, sp.accent.g, sp.accent.b, 0.20f},
                    .pos = {0.0f, -30.0f, 1.0f},
                });
                l.text("title", detail::text_preset(
                    sp.title, 18.0f, 700, {0.90f, 0.92f, 0.98f, 1.0f},
                    TextAlign::Center, {220.0f, 36.0f}, {0.0f, 20.0f, 1.0f}
                ));
                l.depth_reveal(sp.depth_z, Frame{40});
            });
        }

        s.stagger(StaggerConfig{
            .delay_per_unit = Frame{6},
            .easing = EasingCurve{Easing::OutCubic},
        }, StaggerOrder::DepthNearToFar);

        s.layer("hero_title", [](LayerBuilder& l) {
            l.position({0.0f, -350.0f, 0.0f});
            l.text("main", detail::text_preset(
                "Everything your team needs",
                44.0f, 800, {0.95f, 0.96f, 1.0f, 1.0f},
                TextAlign::Center, {900.0f, 70.0f}
            ));
            l.glow(GlowParams{.radius = 18.0f, .intensity = 0.30f, .color = Color{0.3f, 0.6f, 1.0f, 1.0f}, .threshold = 0.0f});
            l.float_idle(8.0f, Frame{100});
        });

        return s.build();
    });
}
