// ── DepthProductReveal — dramatic product reveal with depth layers ─────
// Included inline inside namespace chronon3d::scene_presets.

inline Composition depth_product_reveal() {
    return composition({
        .name = "DepthProductReveal",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 120,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.animated_camera(camera_rig::low_angle_reveal({
            .from_position = {0.0f, -200.0f, -1200.0f},
            .to_position   = {0.0f, 30.0f, -850.0f},
            .from_tilt = 22.0f,
            .to_tilt   = 2.0f,
            .target = {0.0f, 0.0f, 0.0f},
            .zoom = 1000.0f,
            .duration = 100,
            .start_frame = Frame{0},
            .easing = EasingCurve::cubic_bezier(0.33f, 1.0f, 0.68f, 1.0f),
        }));

        s.layer("floor", [](LayerBuilder& l) {
            l.enable_3d(true);
            l.grid_plane("grid", {
                .pos = {0.0f, -250.0f, 0.0f},
                .axis = PlaneAxis::XZ,
                .extent = 2500,
                .spacing = 150,
                .color = {0.30f, 0.55f, 1.0f, 0.12f},
                .fade_distance = 2000.0f,
                .fade_min_alpha = 0.0f,
            });
        });

        s.screen_layer("atmosphere", [](LayerBuilder& l) {
            l.enable_3d(true);
            l.position({0.0f, 0.0f, -500.0f});
            l.rect("ambient", {
                .size = {W * 2.0f, H * 2.0f},
                .color = {0.02f, 0.02f, 0.06f, 1.0f},
            });
        });

        s.layer("product", [](LayerBuilder& l) {
            l.position({0.0f, -60.0f, 50.0f});
            l.enable_3d(true);
            l.accepts_lights(true);
            l.casts_shadows(true);
            l.accepts_shadows(true);

            l.rounded_rect("card", {
                .size = {320.0f, 220.0f}, .radius = 20.0f,
                .color = {0.08f, 0.08f, 0.16f, 0.92f},
                .fill = detail::linear_gradient_v(
                    {0.10f, 0.10f, 0.20f, 0.90f},
                    {0.05f, 0.05f, 0.12f, 0.95f}
                ),
            });
            l.glow(GlowParams{.radius = 32.0f, .intensity = 0.45f, .color = Color{0.3f, 0.5f, 1.0f, 1.0f}, .threshold = 0.0f});
            l.bloom(0.70f, 24.0f, 0.40f);

            l.rounded_rect("icon_bg", {
                .size = {60.0f, 60.0f}, .radius = 14.0f,
                .color = {0.15f, 0.40f, 1.0f, 0.20f},
                .pos = {0.0f, -40.0f, 0.0f},
            });
            l.text("prod_name", detail::text_preset(
                "Product Pro", 22.0f, 700, {0.92f, 0.94f, 1.0f, 1.0f},
                TextAlign::Center, {300.0f, 40.0f}, {0.0f, 30.0f, 0.0f}
            ));
            l.text("tag", detail::text_preset(
                "Next-gen platform", 15.0f, 400, {0.50f, 0.55f, 0.70f, 1.0f},
                TextAlign::Center, {300.0f, 30.0f}, {0.0f, 65.0f, 0.0f}
            ));
            l.card_flip_2_5d(Frame{50});
            l.drop_shadow({0.0f, 20.0f}, {0.0f, 0.0f, 0.0f, 0.35f}, 40.0f);
        });

        struct NodeSpec { std::string name; Vec3 position; Color color; };
        const NodeSpec nodes[] = {
            {"node_0", {-220.0f, -100.0f, 120.0f}, {0.2f, 0.8f, 0.5f, 1.0f}},
            {"node_1", { 230.0f, -120.0f, 150.0f}, {0.8f, 0.3f, 1.0f, 1.0f}},
            {"node_2", {-180.0f,  80.0f, 90.0f},   {1.0f, 0.5f, 0.2f, 1.0f}},
            {"node_3", { 200.0f,  60.0f, 110.0f},  {0.3f, 0.6f, 1.0f, 1.0f}},
        };
        for (const auto& n : nodes) {
            s.layer(n.name, [&n](LayerBuilder& l) {
                l.position(n.position);
                l.enable_3d(true);
                l.circle("dot", {.radius = 6.0f, .color = n.color, .pos = {0.0f, 0.0f, 0.0f}});
                l.glow(GlowParams{.radius = 12.0f, .intensity = 0.60f, .color = n.color, .threshold = 0.0f});
                l.float_idle(10.0f, Frame{80});
            });
        }

        s.layer("hero_title", [](LayerBuilder& l) {
            l.position({0.0f, -300.0f, 0.0f});
            l.enable_3d(true);
            l.text("title", detail::text_preset(
                "The Future of Product", 56.0f, 800, {0.95f, 0.96f, 1.0f, 1.0f},
                TextAlign::Center, {900.0f, 80.0f}
            ));
            l.depth_reveal(300.0f, Frame{45});
        });

        TextAnimator tagline;
        tagline.text("Built for the next generation")
            .font_size(24.0f).font_path(detail::default_font())
            .font_weight(400).color({0.55f, 0.60f, 0.75f, 1.0f})
            .align(TextAlign::Center)
            .config({.mode = TextAnimMode::ByWord, .duration = Frame{20},
                     .delay_per_unit = Frame{6}, .easing = EasingCurve{Easing::OutCubic},
                     .animate_opacity = true, .animate_slide = true,
                     .slide_from = {0.0f, 20.0f, 0.0f}});
        tagline.build(s, "tagline");

        return s.build();
    });
}
