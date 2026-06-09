#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/animation/effects/stagger.hpp>
#include <chronon3d/text/text_animator.hpp>
#include <chronon3d/presets/motion_presets.hpp>

namespace chronon3d::scene_presets {

// ═══════════════════════════════════════════════════════════════════════════════
// Scene Presets — professional pre-composed scenes using the Chronon3D motion
// system, camera rig, stagger, and text animator.
//
// Each preset returns a Composition that can be evaluated at any frame.
//
// Usage:
//   auto comp = scene_presets::saas_intro();
//   Scene scene = comp.evaluate(Frame{45});
// ═══════════════════════════════════════════════════════════════════════════════

constexpr f32 W  = 1920.0f;
constexpr f32 H  = 1080.0f;

// ── Helpers ───────────────────────────────────────────────────────────────────

namespace detail {

inline std::string default_font() { return "assets/fonts/Inter-Bold.ttf"; }

inline Fill soft_radial_gradient(Color inner, Color outer) {
    return Fill::radial({0.5f, 0.5f}, 0.5f, {
        {0.0f, inner},
        {1.0f, outer},
    });
}

inline Fill linear_gradient_v(Color top, Color bottom) {
    return Fill::linear({0.0f, 0.0f}, {0.0f, 1.0f}, {
        {0.0f, top},
        {1.0f, bottom},
    });
}

inline Fill linear_gradient_h(Color left, Color right) {
    return Fill::linear({0.0f, 0.0f}, {1.0f, 0.0f}, {
        {0.0f, left},
        {1.0f, right},
    });
}

// Helper: create TextParams with minimal boilerplate.
// Uses named fields in declaration order to satisfy C++20 designated initializers.
inline TextParams text_preset(
    std::string text,
    f32 font_size,
    int font_weight,
    Color color,
    TextAlign align,
    Vec2 size = {600.0f, 60.0f},
    Vec3 pos = {0.0f, 0.0f, 0.0f},
    std::string font_path = default_font(),
    std::string font_family = "Inter",
    std::string font_style = "normal"
) {
    TextParams tp;
    tp.text = std::move(text);
    tp.size = size;
    tp.pos = pos;
    tp.font_path = std::move(font_path);
    tp.font_family = std::move(font_family);
    tp.font_weight = font_weight;
    tp.font_style = std::move(font_style);
    tp.font_size = font_size;
    tp.color = color;
    tp.align = align;
    tp.vertical_align = VerticalAlign::Middle;
    return tp;
}

} // namespace detail

// ═══════════════════════════════════════════════════════════════════════════════
// 1. SaaSIntro — modern SaaS product hero
// ═══════════════════════════════════════════════════════════════════════════════
inline Composition saas_intro() {
    return composition({
        .name = "SaaSIntro",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 120,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Camera: push-in (all fields in order, no skipping)
        s.animated_camera(camera_rig::hero_push_in({
            .from_position = {0.0f, 0.0f, -1100.0f},
            .to_position   = {0.0f, 0.0f, -750.0f},
            .from_tilt = -3.0f,
            .to_tilt   = 1.0f,
            .from_yaw  = 0.0f,
            .to_yaw    = 2.0f,
            .zoom = 1000.0f,
            .duration = 100,
            .start_frame = Frame{0},      // explicit to avoid skipping
            .easing = EasingCurve::cubic_bezier(0.16f, 1.0f, 0.3f, 1.0f),
        }));

        // Background: deep dark gradient
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

        // Geometric accents
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

        // Main title: per-character reveal via TextAnimator
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
                .easing = EasingCurve{Easing::OutCubic},
                .animate_opacity = true,
                .animate_slide = true,
                .slide_from = {0.0f, 50.0f, 0.0f},
                .animate_scale = true,
                .scale_from = {0.85f, 0.85f, 1.0f},
            });
        title_anim.build(s, "title");

        // Subtitle: slide in
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

        // Feature cards (3 cards with soft_pop)
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

        // CTA button
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

// ═══════════════════════════════════════════════════════════════════════════════
// 2. FloatingCardsHero — cards floating in 3D space with parallax
// ═══════════════════════════════════════════════════════════════════════════════
inline Composition floating_cards_hero() {
    return composition({
        .name = "FloatingCardsHero",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 150,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Camera: gentle float (SubtleFloatParams has no intermediate fields to skip)
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

        // Background grid
        s.grid_background("grid", {
            .size = {W, H},
            .bg_color = {0.02f, 0.02f, 0.04f, 1.0f},
            .grid_color = {0.30f, 0.55f, 1.0f, 0.04f},
            .spacing = 120.0f,
            .minor_thickness = 1.0f,
            .major_thickness = 2.5f,
            .major_every = 4,
        });

        // 4 cards at varying Z depths
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
                    .size = {240.0f, 3.0f},
                    .radius = 0.0f,
                    .color = sp.accent,
                    .pos = {0.0f, -78.5f, 0.0f},
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

        // Stagger by depth
        s.stagger(StaggerConfig{
            .delay_per_unit = Frame{6},
            .easing = EasingCurve{Easing::OutCubic},
        }, StaggerOrder::DepthNearToFar);

        // Floating title
        s.layer("hero_title", [](LayerBuilder& l) {
            l.position({0.0f, -350.0f, 0.0f});
            l.text("main", detail::text_preset(
                "Everything your team needs",
                44.0f, 800, {0.95f, 0.96f, 1.0f, 1.0f},
                TextAlign::Center, {900.0f, 70.0f}
            ));
            l.glow(18.0f, 0.30f, Color{0.3f, 0.6f, 1.0f, 1.0f}, 0.0f);
            l.float_idle(8.0f, Frame{100});
        });

        return s.build();
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// 3. KineticTitle — kinetic typography with per-character animation
// ═══════════════════════════════════════════════════════════════════════════════
inline Composition kinetic_title() {
    return composition({
        .name = "KineticTitle",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Camera: focus pull (all fields in order)
        s.animated_camera(camera_rig::focus_pull({
            .position = {0.0f, 0.0f, -1000.0f},
            .zoom = 1000.0f,
            .from_focus_z = 0.0f,
            .to_focus_z = 200.0f,
            .aperture = 0.02f,
            .max_blur = 16.0f,
            .duration = 90,
            .start_frame = Frame{0},       // explicit to avoid skipping
            .easing = EasingCurve{Easing::InOutCubic},
        }));

        // Background
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

        // Back glow
        s.screen_layer("back_glow", [](LayerBuilder& l) {
            l.blend(BlendMode::Add);
            l.opacity(0.15f);
            l.circle("glow", {
                .radius = 350.0f,
                .color = {0.40f, 0.25f, 0.90f, 0.15f},
                .pos = {0.0f, 0.0f, 0.0f},
            });
        });

        // Main title: kinetic per-character
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
                .easing = EasingCurve{Easing::OutBack},
                .animate_opacity = true,
                .animate_slide = false,          // explicit to avoid skipping fields
                .slide_from = {0.0f, 40.0f, 0.0f},
                .animate_scale = true,
                .scale_from = {0.5f, 0.5f, 1.0f},
                .animate_tracking = true,
                .tracking_from = 25.0f,
            });
        kinetic.build(s, "kinetic_title");

        // Emphasis word with glow
        s.layer("emphasis", [](LayerBuilder& l) {
            l.position({215.0f, 0.0f, 0.0f});
            l.text("emp", detail::text_preset(
                "Impact", 100.0f, 900, {0.60f, 0.35f, 1.0f, 0.90f},
                TextAlign::Center, {400.0f, 110.0f}
            ));
            l.glow(24.0f, 0.40f, Color{0.50f, 0.30f, 1.0f, 1.0f}, 0.0f);
            l.bloom(0.60f, 20.0f, 0.35f);
            l.settle(0.08f, Frame{30});
        });

        // Subtitle: per-word stagger
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

// ═══════════════════════════════════════════════════════════════════════════════
// 4. DepthProductReveal — dramatic product reveal with depth layers
// ═══════════════════════════════════════════════════════════════════════════════
inline Composition depth_product_reveal() {
    return composition({
        .name = "DepthProductReveal",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 120,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Camera: low-angle reveal (all fields in order)
        s.animated_camera(camera_rig::low_angle_reveal({
            .from_position = {0.0f, -200.0f, -1200.0f},
            .to_position   = {0.0f, 30.0f, -850.0f},
            .from_tilt = 22.0f,
            .to_tilt   = 2.0f,
            .target = {0.0f, 0.0f, 0.0f},     // explicit to avoid skipping
            .zoom = 1000.0f,
            .duration = 100,
            .start_frame = Frame{0},           // explicit to avoid skipping
            .easing = EasingCurve::cubic_bezier(0.33f, 1.0f, 0.68f, 1.0f),
        }));

        // Grid floor
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

        // Background atmosphere
        s.screen_layer("atmosphere", [](LayerBuilder& l) {
            l.enable_3d(true);
            l.position({0.0f, 0.0f, -500.0f});
            l.rect("ambient", {
                .size = {W * 2.0f, H * 2.0f},
                .color = {0.02f, 0.02f, 0.06f, 1.0f},
            });
        });

        // Main product card: flip in
        s.layer("product", [](LayerBuilder& l) {
            l.position({0.0f, -60.0f, 50.0f});
            l.enable_3d(true);
            l.accepts_lights(true);
            l.casts_shadows(true);
            l.accepts_shadows(true);

            l.rounded_rect("card", {
                .size = {320.0f, 220.0f},
                .radius = 20.0f,
                .color = {0.08f, 0.08f, 0.16f, 0.92f},
                .fill = detail::linear_gradient_v(
                    {0.10f, 0.10f, 0.20f, 0.90f},
                    {0.05f, 0.05f, 0.12f, 0.95f}
                ),
            });
            l.glow(32.0f, 0.45f, Color{0.3f, 0.5f, 1.0f, 1.0f}, 0.0f);
            l.bloom(0.70f, 24.0f, 0.40f);

            l.rounded_rect("icon_bg", {
                .size = {60.0f, 60.0f},
                .radius = 14.0f,
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

        // Floating nodes around product
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
                l.circle("dot", {
                    .radius = 6.0f, .color = n.color, .pos = {0.0f, 0.0f, 0.0f},
                });
                l.glow(12.0f, 0.60f, n.color, 0.0f);
                l.float_idle(10.0f, Frame{80});
            });
        }

        // Title above
        s.layer("hero_title", [](LayerBuilder& l) {
            l.position({0.0f, -300.0f, 0.0f});
            l.enable_3d(true);
            l.text("title", detail::text_preset(
                "The Future of Product", 56.0f, 800, {0.95f, 0.96f, 1.0f, 1.0f},
                TextAlign::Center, {900.0f, 80.0f}
            ));
            l.depth_reveal(300.0f, Frame{45});
        });

        // Tagline: TextAnimator per-word
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

// ═══════════════════════════════════════════════════════════════════════════════
// 5. AppleStyleHero — clean, minimal Apple-style hero section
// ═══════════════════════════════════════════════════════════════════════════════
inline Composition apple_style_hero() {
    return composition({
        .name = "AppleStyleHero",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 100,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Camera: parallax pan (all fields in order)
        s.animated_camera(camera_rig::parallax_pan({
            .from_position = {-20.0f, 0.0f, -1000.0f},
            .to_position   = { 20.0f, 0.0f, -1000.0f},
            .target = {0.0f, 0.0f, 0.0f},        // explicit to avoid skipping
            .zoom = 1000.0f,
            .duration = 100,
            .start_frame = Frame{0},               // explicit to avoid skipping
            .easing = EasingCurve{Easing::InOutSine},
        }));

        // Background: clean white gradient
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

        // Subtle accent shapes
        s.layer("accent_1", [](LayerBuilder& l) {
            l.opacity(0.06f);
            l.rounded_rect("shape", {
                .size = {400.0f, 400.0f},
                .radius = 200.0f,
                .color = {0.3f, 0.5f, 1.0f, 1.0f},
                .pos = {750.0f, -300.0f, 0.0f},
            });
        });
        s.layer("accent_2", [](LayerBuilder& l) {
            l.opacity(0.04f);
            l.rounded_rect("shape", {
                .size = {250.0f, 250.0f},
                .radius = 125.0f,
                .color = {0.6f, 0.3f, 1.0f, 1.0f},
                .pos = {-700.0f, 350.0f, 0.0f},
            });
        });

        // Hero title: staggered by word via TextAnimator
        TextAnimator hero_title;
        hero_title.text("Think Different.")
            .font_size(88.0f).font_path(detail::default_font())
            .font_weight(800).color({0.05f, 0.05f, 0.08f, 1.0f})
            .align(TextAlign::Center)
            .config({.mode = TextAnimMode::ByWord, .duration = Frame{25},
                     .delay_per_unit = Frame{10}, .easing = EasingCurve{Easing::OutCubic},
                     .animate_opacity = true, .animate_slide = true,
                     .slide_from = {0.0f, 30.0f, 0.0f},
                     .animate_scale = true, .scale_from = {0.92f, 0.92f, 1.0f}});
        hero_title.build(s, "hero");

        // Subtitle: slide in
        s.layer("subtitle", [](LayerBuilder& l) {
            l.position({0.0f, 105.0f, 0.0f});
            l.text("sub", detail::text_preset(
                "The most powerful creative tool. Ever.",
                26.0f, 400, {0.45f, 0.45f, 0.50f, 1.0f},
                TextAlign::Center, {700.0f, 50.0f}
            ));
            l.slide_in({0.0f, 25.0f, 0.0f}, Frame{35}, EasingCurve{Easing::OutCubic});
        });

        // Primary CTA button
        s.layer("cta_primary", [](LayerBuilder& l) {
            l.position({-90.0f, 220.0f, 0.0f});
            l.rounded_rect("pill", {
                .size = {150.0f, 50.0f},
                .radius = 25.0f,
                .color = {0.05f, 0.05f, 0.08f, 1.0f},
            });
            l.text("label", detail::text_preset(
                "Learn More", 16.0f, 600, {1.0f, 1.0f, 1.0f, 1.0f},
                TextAlign::Center, {150.0f, 50.0f}
            ));
            l.soft_pop(Frame{30});
        });

        // Secondary CTA (outline)
        s.layer("cta_secondary", [](LayerBuilder& l) {
            l.position({90.0f, 220.0f, 0.0f});
            l.rounded_rect("outline", {
                .size = {130.0f, 50.0f},
                .radius = 25.0f,
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

} // namespace chronon3d::scene_presets
