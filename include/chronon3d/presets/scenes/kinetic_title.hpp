// ── KineticTitle — kinetic typography with per-character animation ─────
// Included inline inside namespace chronon3d::scene_presets.

inline Composition kinetic_title() {
    return composition({
        .name = "KineticTitle",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.animated_camera(camera_rig::focus_pull({
            .position = {0.0f, 0.0f, -1000.0f},
            .zoom = 1000.0f,
            .from_focus_z = 0.0f,
            .to_focus_z = 200.0f,
            .aperture = 0.02f,
            .max_blur = 16.0f,
            .duration = 90,
            .start_frame = Frame{0},
            .easing = EasingCurve{Easing::InOutCubic},
        }));

        s.screen_layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {
                .size = {W, H},
                .color = {0.01f, 0.01f, 0.04f, 1.0f},
                .fill = detail::soft_radial_gradient(
                    {0.04f, 0.03f, 0.10f, 1.0f},
                    {0.01f, 0.01f, 0.03f, 1.0f}
                ),
            });
        });

        s.screen_layer("back_glow", [](LayerBuilder& l) {
            l.blend(BlendMode::Add);
            l.opacity(0.15f);
            l.circle("glow", {
                .radius = 350.0f,
                .color = {0.40f, 0.25f, 0.90f, 0.15f},
                .pos = {0.0f, 0.0f, 0.0f},
            });
        });

        TextAnimator kinetic;
        kinetic
            .text("Create Impact")
            .font_size(100.0f)
            .font_path(detail::default_font())
            .font_weight(900)
            .color({1.0f, 1.0f, 1.0f, 1.0f})
            .align(TextAlign::Center)
            .config({
                .mode = TextAnimMode::ByCharacter,
                .duration = Frame{20},
                .delay_per_unit = Frame{2},
                .easing = Easing::OutBack,
                .animate_opacity = true,
                .animate_slide = false,
                .slide_from = {0.0f, 40.0f, 0.0f},
                .animate_scale = true,
                .scale_from = {0.5f, 0.5f, 1.0f},
                .animate_tracking = true,
                .tracking_from = 25.0f,
            });
        kinetic.build(s, "kinetic_title");

        s.layer("emphasis", [](LayerBuilder& l) {
            l.position({215.0f, 0.0f, 0.0f});
            l.text("emp", detail::text_preset(
                "Impact", 100.0f, 900, {0.60f, 0.35f, 1.0f, 0.90f},
                TextAlign::Center, {400.0f, 110.0f}
            ));
            l.glow(GlowParams{.radius = 24.0f, .intensity = 0.40f, .color = Color{0.50f, 0.30f, 1.0f, 1.0f}, .threshold = 0.0f});
            l.bloom(0.60f, 20.0f, 0.35f);
            l.settle(0.08f, Frame{30});
        });

        s.layer("subtitle", [](LayerBuilder& l) {
            l.position({0.0f, 100.0f, 0.0f});
            std::vector<std::string> words = {"Tell", "stories", "that", "matter."};
            f32 word_x = -300.0f;
            for (size_t i = 0; i < words.size(); ++i) {
                f32 w = static_cast<f32>(words[i].size()) * 22.0f;
                l.text("w" + std::to_string(i), detail::text_preset(
                    words[i], 28.0f, 400, {0.50f, 0.55f, 0.70f, 1.0f},
                    TextAlign::Center, {w + 10.0f, 50.0f}, {word_x, 0.0f, 0.0f}
                ));
                Frame delay = Frame{static_cast<Frame>(i * 8)};
                auto& op = l.opacity_anim();
                op.key(Frame{0},   0.0f, EasingCurve{Easing::Hold});
                op.key(delay,      0.0f, EasingCurve{Easing::OutCubic});
                op.key(delay+20,   1.0f, EasingCurve{Easing::Linear});
                auto& pos = l.position_anim();
                pos.key(Frame{0},  Vec3{word_x, 20.0f, 0.0f}, EasingCurve{Easing::Hold});
                pos.key(delay,     Vec3{word_x, 20.0f, 0.0f}, EasingCurve{Easing::OutCubic});
                pos.key(delay+20,  Vec3{word_x, 0.0f, 0.0f},  EasingCurve{Easing::Linear});
                word_x += w + 14.0f;
            }
        });

        return s.build();
    });
}
