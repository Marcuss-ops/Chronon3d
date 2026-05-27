#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/math/math_base.hpp>
#include <chronon3d/math/math_base.hpp>
#include <xxhash.h>
#include <cmath>
#include <iostream>

using namespace chronon3d;

namespace {

u64 fb_hash(const Framebuffer& fb) {
    return XXH64(fb.pixels_row(0), fb.size_bytes(), 0);
}

} // namespace

// 1. transform_inverse_roundtrip
TEST_CASE("Invariants: transform_inverse_roundtrip") {
    // Identity matrix
    Mat4 I(1.0f);
    Vec4 p1{5.0f, -3.0f, 2.0f, 1.0f};
    Vec4 roundtrip1 = glm::inverse(I) * (I * p1);
    CHECK(roundtrip1.x == doctest::Approx(p1.x));
    CHECK(roundtrip1.y == doctest::Approx(p1.y));
    CHECK(roundtrip1.z == doctest::Approx(p1.z));
    CHECK(roundtrip1.w == doctest::Approx(p1.w));

    // Translation
    Mat4 T = math::translate(Vec3(10.0f, -20.0f, 5.0f));
    Vec4 p2{1.0f, 2.0f, 3.0f, 1.0f};
    Vec4 roundtrip2 = glm::inverse(T) * (T * p2);
    CHECK(roundtrip2.x == doctest::Approx(p2.x));
    CHECK(roundtrip2.y == doctest::Approx(p2.y));
    CHECK(roundtrip2.z == doctest::Approx(p2.z));
    CHECK(roundtrip2.w == doctest::Approx(p2.w));

    // Scale
    Mat4 S = math::scale(Vec3(2.5f, 0.5f, 4.0f));
    Vec4 p3{10.0f, 10.0f, 10.0f, 1.0f};
    Vec4 roundtrip3 = glm::inverse(S) * (S * p3);
    CHECK(roundtrip3.x == doctest::Approx(p3.x));
    CHECK(roundtrip3.y == doctest::Approx(p3.y));
    CHECK(roundtrip3.z == doctest::Approx(p3.z));

    // Rotation (90 degrees around Z axis, e.g. rotating (1,0) should result in (0,1))
    Quat q = glm::angleAxis(glm::radians(90.0f), Vec3(0.0f, 0.0f, 1.0f));
    Mat4 R = math::rotate(q);
    Vec4 p4{1.0f, 0.0f, 0.0f, 1.0f};
    Vec4 rotated = R * p4;
    CHECK(rotated.x == doctest::Approx(0.0f));
    CHECK(rotated.y == doctest::Approx(1.0f));
    CHECK(rotated.z == doctest::Approx(0.0f));

    Vec4 roundtrip4 = glm::inverse(R) * rotated;
    CHECK(roundtrip4.x == doctest::Approx(p4.x));
    CHECK(roundtrip4.y == doctest::Approx(p4.y));
    CHECK(roundtrip4.z == doctest::Approx(p4.z));
}

// 2. bbox_expands_with_glow_radius
TEST_CASE("Invariants: bbox_expands_with_glow_radius") {
    using namespace chronon3d::graph;
    
    RenderGraphContext ctx;
    ctx.width = 200;
    ctx.height = 200;

    std::optional<raster::BBox> input_bbox = raster::BBox{80, 80, 120, 120};
    std::vector<std::optional<raster::BBox>> inputs = {input_bbox};

    // Glow with radius 5
    EffectStack effects1;
    effects1.push_back(EffectInstance{GlowParams{.radius = 5.0f, .intensity = 0.8f, .color = Color::white()}});
    EffectStackNode node1(effects1);

    // Glow with radius 15
    EffectStack effects2;
    effects2.push_back(EffectInstance{GlowParams{.radius = 15.0f, .intensity = 0.8f, .color = Color::white()}});
    EffectStackNode node2(effects2);

    auto bbox1 = node1.predicted_bbox(ctx, inputs);
    auto bbox2 = node2.predicted_bbox(ctx, inputs);

    REQUIRE(bbox1.has_value());
    REQUIRE(bbox2.has_value());

    // Larger glow radius must result in a larger bounding box
    CHECK(bbox2->x0 < bbox1->x0);
    CHECK(bbox2->y0 < bbox1->y0);
    CHECK(bbox2->x1 > bbox1->x1);
    CHECK(bbox2->y1 > bbox1->y1);
}

// 3. alpha_zero_noop
TEST_CASE("Invariants: alpha_zero_noop") {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    // Scene with ONLY background
    Composition comp_bg({.width = 100, .height = 100, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("bg_rect", {.size = {100, 100}, .color = Color{0.1f, 0.2f, 0.3f, 1.0f}});
        });
        return s.build();
    });

    // Scene with background + foreground with opacity 0.0f
    Composition comp_fg_opacity0({.width = 100, .height = 100, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("bg_rect", {.size = {100, 100}, .color = Color{0.1f, 0.2f, 0.3f, 1.0f}});
        });
        s.layer("fg", [](LayerBuilder& l) {
            l.opacity(0.0f);
            l.rect("fg_rect", {.size = {50, 50}, .color = Color::red()});
        });
        return s.build();
    });

    auto fb_bg = renderer.render_frame(comp_bg, 0);
    auto fb_fg = renderer.render_frame(comp_fg_opacity0, 0);

    REQUIRE(fb_bg != nullptr);
    REQUIRE(fb_fg != nullptr);

    if (fb_hash(*fb_bg) != fb_hash(*fb_fg)) {
        int diff_count = 0;
        for (int y = 0; y < fb_bg->height(); ++y) {
            for (int x = 0; x < fb_bg->width(); ++x) {
                Color c_bg = fb_bg->get_pixel(x, y);
                Color c_fg = fb_fg->get_pixel(x, y);
                if (std::abs(c_bg.r - c_fg.r) > 1e-4f ||
                    std::abs(c_bg.g - c_fg.g) > 1e-4f ||
                    std::abs(c_bg.b - c_fg.b) > 1e-4f ||
                    std::abs(c_bg.a - c_fg.a) > 1e-4f) {
                    if (diff_count < 10) {
                        std::cout << "Diff at (" << x << ", " << y << "): bg=["
                                  << c_bg.r << ", " << c_bg.g << ", " << c_bg.b << ", " << c_bg.a
                                  << "], fg=["
                                  << c_fg.r << ", " << c_fg.g << ", " << c_fg.b << ", " << c_fg.a << "]\n";
                    }
                    diff_count++;
                }
            }
        }
        std::cout << "Total differing pixels: " << diff_count << "\n";
    }

    CHECK(fb_hash(*fb_bg) == fb_hash(*fb_fg));
}

// 4. blur_radius_zero_noop
TEST_CASE("Invariants: blur_radius_zero_noop") {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    Composition comp_no_blur({.width = 100, .height = 100, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            l.rect("r", {.size = {50, 50}, .color = Color::white()});
        });
        return s.build();
    });

    Composition comp_blur0({.width = 100, .height = 100, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            l.blur(0.0f);
            l.rect("r", {.size = {50, 50}, .color = Color::white()});
        });
        return s.build();
    });

    auto fb_no_blur = renderer.render_frame(comp_no_blur, 0);
    auto fb_blur0 = renderer.render_frame(comp_blur0, 0);

    REQUIRE(fb_no_blur != nullptr);
    REQUIRE(fb_blur0 != nullptr);
    CHECK(fb_hash(*fb_no_blur) == fb_hash(*fb_blur0));
}

// 5. glow_intensity_zero_noop
TEST_CASE("Invariants: glow_intensity_zero_noop") {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    Composition comp_no_glow({.width = 100, .height = 100, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            l.rect("r", {.size = {50, 50}, .color = Color::white()});
        });
        return s.build();
    });

    Composition comp_glow_intensity0({.width = 100, .height = 100, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            l.glow(10.0f, 0.0f);
            l.rect("r", {.size = {50, 50}, .color = Color::white()});
        });
        return s.build();
    });

    auto fb_no_glow = renderer.render_frame(comp_no_glow, 0);
    auto fb_glow0 = renderer.render_frame(comp_glow_intensity0, 0);

    REQUIRE(fb_no_glow != nullptr);
    REQUIRE(fb_glow0 != nullptr);
    CHECK(fb_hash(*fb_no_glow) == fb_hash(*fb_glow0));
}

// 6. bloom_threshold_above_max_noop
TEST_CASE("Invariants: bloom_threshold_above_max_noop") {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    Composition comp_no_bloom({.width = 100, .height = 100, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            l.rect("r", {.size = {50, 50}, .color = Color::white()});
        });
        return s.build();
    });

    Composition comp_bloom_high({.width = 100, .height = 100, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            l.bloom(2.0f, 10.0f, 1.0f);
            l.rect("r", {.size = {50, 50}, .color = Color::white()});
        });
        return s.build();
    });

    auto fb_no_bloom = renderer.render_frame(comp_no_bloom, 0);
    auto fb_bloom_high = renderer.render_frame(comp_bloom_high, 0);

    REQUIRE(fb_no_bloom != nullptr);
    REQUIRE(fb_bloom_high != nullptr);
    CHECK(fb_hash(*fb_no_bloom) == fb_hash(*fb_bloom_high));
}

// 7. effect_order_changes_hash
TEST_CASE("Invariants: effect_order_changes_hash") {
    using namespace chronon3d::graph;

    LayerBuilder lb1("l1");
    lb1.blur(10.0f).tint(Color::red(), 1.0f);
    auto l1 = lb1.build();

    LayerBuilder lb2("l2");
    lb2.tint(Color::red(), 1.0f).blur(10.0f);
    auto l2 = lb2.build();

    u64 hash1 = hash_effect_stack(l1.effects);
    u64 hash2 = hash_effect_stack(l2.effects);

    CHECK(hash1 != hash2);
}

// 8. cache_key_changes_when_glow_params_change
TEST_CASE("Invariants: cache_key_changes_when_glow_params_change") {
    using namespace chronon3d::graph;

    // Base glow params
    LayerBuilder lb_base("l");
    lb_base.glow(10.0f, 0.8f, Color::white());
    u64 hash_base = hash_effect_stack(lb_base.build().effects);

    // Different radius
    LayerBuilder lb_radius("l");
    lb_radius.glow(15.0f, 0.8f, Color::white());
    u64 hash_radius = hash_effect_stack(lb_radius.build().effects);

    // Different intensity
    LayerBuilder lb_intensity("l");
    lb_intensity.glow(10.0f, 0.5f, Color::white());
    u64 hash_intensity = hash_effect_stack(lb_intensity.build().effects);

    // Different color
    LayerBuilder lb_color("l");
    lb_color.glow(10.0f, 0.8f, Color::red());
    u64 hash_color = hash_effect_stack(lb_color.build().effects);

    CHECK(hash_base != hash_radius);
    CHECK(hash_base != hash_intensity);
    CHECK(hash_base != hash_color);
}

// 8b. cache_key_changes_when_glow_quality_params_change
TEST_CASE("Invariants: cache_key_changes_when_glow_quality_params_change") {
    using namespace chronon3d::graph;

    LayerBuilder lb_base("l");
    lb_base.glow(GlowPresets::neon_blue(40.0f));
    u64 hash_base = hash_effect_stack(lb_base.build().effects);

    LayerBuilder lb_falloff("l");
    GlowParams falloff = GlowPresets::neon_blue(40.0f);
    falloff.falloff = 1.20f;
    lb_falloff.glow(falloff);
    u64 hash_falloff = hash_effect_stack(lb_falloff.build().effects);

    LayerBuilder lb_core("l");
    GlowParams core = GlowPresets::neon_blue(40.0f);
    core.core_strength = 0.95f;
    lb_core.glow(core);
    u64 hash_core = hash_effect_stack(lb_core.build().effects);

    LayerBuilder lb_screen("l");
    GlowParams screen = GlowPresets::neon_blue(40.0f);
    screen.additive = false;
    lb_screen.glow(screen);
    u64 hash_screen = hash_effect_stack(lb_screen.build().effects);

    CHECK(hash_base != hash_falloff);
    CHECK(hash_base != hash_core);
    CHECK(hash_base != hash_screen);
}

// 9. dirty_rect_contains_glow_spread
TEST_CASE("Invariants: dirty_rect_contains_glow_spread") {
    using namespace chronon3d::graph;

    RenderGraphContext ctx;
    ctx.width = 500;
    ctx.height = 500;

    std::optional<raster::BBox> input_bbox = raster::BBox{100, 100, 200, 200};
    std::vector<std::optional<raster::BBox>> inputs = {input_bbox};

    // Glow with radius 30
    LayerBuilder lb("l");
    lb.glow(30.0f, 0.8f, Color::white());
    auto layer = lb.build();

    EffectStackNode node(layer.effects);
    auto bbox = node.predicted_bbox(ctx, inputs);

    REQUIRE(bbox.has_value());
    // Glow spread must expand the bounding box by at least the glow radius (30 pixels) on each side
    CHECK(bbox->x0 <= 100 - 30);
    CHECK(bbox->y0 <= 100 - 30);
    CHECK(bbox->x1 >= 200 + 30);
    CHECK(bbox->y1 >= 200 + 30);
}

// 10. no_nan_after_effect_stack
TEST_CASE("Invariants: no_nan_after_effect_stack") {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    Composition comp({.width = 100, .height = 100, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            l.blur(5.0f)
             .tint(Color::red(), 0.5f)
             .brightness(0.2f)
             .contrast(1.2f)
             .glow(10.0f, 0.8f, Color::yellow())
             .bloom(0.5f, 15.0f, 0.7f);
            l.rect("r", {.size = {40, 40}, .color = Color::white()});
        });
        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    bool has_nan = false;
    for (int y = 0; y < fb->height(); ++y) {
        for (int x = 0; x < fb->width(); ++x) {
            Color c = fb->get_pixel(x, y);
            if (std::isnan(c.r) || std::isnan(c.g) || std::isnan(c.b) || std::isnan(c.a)) {
                has_nan = true;
            }
        }
    }

    CHECK_FALSE(has_nan);
}
