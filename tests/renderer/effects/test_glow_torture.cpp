#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <tests/helpers/test_utils.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

using namespace chronon3d;

namespace {

float luma(Color c) {
    return (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) * c.a;
}

std::shared_ptr<Framebuffer> render_glow_comp(
    std::function<Scene(const FrameContext&)> build,
    int width = 320,
    int height = 180,
    int frame = 0,
    int duration = 1
) {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    Composition comp({.width = width, .height = height, .duration = duration}, std::move(build));
    return renderer.render_frame(comp, frame);
}

float max_luma(const Framebuffer& fb) {
    float out = 0.0f;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            out = std::max(out, luma(fb.get_pixel(x, y)));
        }
    }
    return out;
}

float mean_core_error(const Framebuffer& expected, const Framebuffer& actual, float threshold) {
    double err = 0.0;
    int count = 0;
    for (int y = 0; y < expected.height(); ++y) {
        for (int x = 0; x < expected.width(); ++x) {
            const Color e = expected.get_pixel(x, y);
            if (luma(e) <= threshold) continue;
            const Color a = actual.get_pixel(x, y);
            err += std::abs(e.r - a.r) + std::abs(e.g - a.g) + std::abs(e.b - a.b);
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(err / static_cast<double>(count * 3)) : 0.0f;
}

int count_colored_halo_pixels(const Framebuffer& fb, const Framebuffer& source, float source_threshold, Color color_hint) {
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            if (luma(source.get_pixel(x, y)) > source_threshold) continue;

            const Color c = fb.get_pixel(x, y);
            const float color_energy = c.r * color_hint.r + c.g * color_hint.g + c.b * color_hint.b;
            if (color_energy > 0.025f && c.a > 0.01f) {
                ++count;
            }
        }
    }
    return count;
}

float weighted_centroid_x(const Framebuffer& fb, float bg_luma) {
    double sum_x = 0.0;
    double weight = 0.0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            const float w = std::max(0.0f, luma(fb.get_pixel(x, y)) - bg_luma);
            sum_x += static_cast<double>(x) * w;
            weight += w;
        }
    }
    return weight > 0.0 ? static_cast<float>(sum_x / weight) : -1.0f;
}

float radial_channel_sample(const Framebuffer& fb, int cx, int cy, int dx, int channel) {
    const Color c = fb.get_pixel(cx + dx, cy);
    if (channel == 0) return c.r;
    if (channel == 1) return c.g;
    return c.b;
}

} // namespace

TEST_CASE("GlowTorture: neon text preserves a sharp core and creates a cyan halo") {
    auto make_scene = [](bool glow) {
        return [glow](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.005f, 0.010f, 0.035f, 1.0f});
            });

            s.layer("text", [glow](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                if (glow) {
                    GlowParams g = GlowPresets::neon_blue(34.0f);
                    g.additive = false;
                    l.glow(g);
                }
                l.text("word", {
                    .text = "CHRONON3D",
                    .size = {300.0f, 90.0f},
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_size = 54.0f,
                    .color = Color{0.96f, 0.99f, 1.0f, 1.0f},
                    .align = TextAlign::Center,
                    .vertical_align = VerticalAlign::Middle
                });
            });
            return s.build();
        };
    };

    auto source = render_glow_comp(make_scene(false));
    auto glow = render_glow_comp(make_scene(true));
    REQUIRE(source != nullptr);
    REQUIRE(glow != nullptr);

    CHECK(max_luma(*glow) >= max_luma(*source) * 0.95f);
    CHECK(mean_core_error(*source, *glow, 0.65f) < 0.025f);
    CHECK(count_colored_halo_pixels(*glow, *source, 0.08f, Color{0.1f, 0.8f, 1.0f, 1.0f}) > 900);
}

TEST_CASE("GlowTorture: tiny bright text stays readable") {
    auto make_scene = [](bool glow) {
        return [glow](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.006f, 0.008f, 0.020f, 1.0f});
            });

            s.layer("tiny", [glow](LayerBuilder& l) {
                if (glow) {
                    GlowParams g = GlowPresets::soft_cyan(10.0f);
                    g.intensity = 0.65f;
                    g.additive = false;
                    l.glow(g);
                }
                l.text("tiny_text", {
                    .text = "small readable glow",
                    .size = {220.0f, 34.0f},
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_size = 20.0f,
                    .color = Color::white(),
                    .align = TextAlign::Center,
                    .vertical_align = VerticalAlign::Middle
                });
            });
            return s.build();
        };
    };

    auto source = render_glow_comp(make_scene(false), 260, 120);
    auto glow = render_glow_comp(make_scene(true), 260, 120);
    REQUIRE(source != nullptr);
    REQUIRE(glow != nullptr);

    CHECK(mean_core_error(*source, *glow, 0.55f) < 0.035f);
    CHECK(max_luma(*glow) > 0.85f);
    CHECK(count_colored_halo_pixels(*glow, *source, 0.06f, Color{0.2f, 0.9f, 1.0f, 1.0f}) > 120);
}

TEST_CASE("GlowTorture: large radius glow has smooth monotonic falloff") {
    auto fb = render_glow_comp([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.004f, 0.006f, 0.016f, 1.0f});
        });

        s.layer("orb", [](LayerBuilder& l) {
            GlowParams g = GlowPresets::soft_cyan(120.0f);
            g.intensity = 1.15f;
            g.bloom_strength = 0.30f;
            g.outer_downscale = 0.25f;
            g.additive = false;
            l.glow(g);
            l.circle("core", {
                .radius = 20.0f,
                .color = Color{1.0f, 1.0f, 1.0f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });
        return s.build();
    }, 360, 220);

    REQUIRE(fb != nullptr);
    const int cx = fb->width() / 2;
    const int cy = fb->height() / 2;
    float prev = std::numeric_limits<float>::infinity();
    for (int d : {32, 52, 72, 96, 124, 150}) {
        const float b = radial_channel_sample(*fb, cx, cy, d, 2);
        CHECK(b <= prev + 0.035f);
        prev = b;
    }
    CHECK(radial_channel_sample(*fb, cx, cy, 52, 2) > radial_channel_sample(*fb, cx, cy, 124, 2));
    CHECK(radial_channel_sample(*fb, cx, cy, 132, 2) > 0.001f);
}

TEST_CASE("GlowTorture: colored glow stack keeps saturation in overlap") {
    auto fb = render_glow_comp([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.006f, 0.006f, 0.014f, 1.0f});
        });

        auto orb = [&](const char* layer, Vec3 pos, Color glow) {
            s.layer(layer, [=](LayerBuilder& l) {
                l.position(pos).blend(BlendMode::Screen).glow(38.0f, 1.60f, glow);
                l.circle("c", {
                    .radius = 14.0f,
                    .color = Color{0.98f, 0.98f, 1.0f, 0.82f},
                    .pos = {0.0f, 0.0f, 0.0f}
                });
            });
        };

        orb("red", {-22.0f, 0.0f, 0.0f}, Color{1.0f, 0.05f, 0.08f, 1.0f});
        orb("blue", {22.0f, 0.0f, 0.0f}, Color{0.05f, 0.25f, 1.0f, 1.0f});
        orb("violet", {0.0f, 18.0f, 0.0f}, Color{0.70f, 0.12f, 1.0f, 1.0f});
        return s.build();
    }, 240, 180);

    REQUIRE(fb != nullptr);
    const Color c = fb->get_pixel(fb->width() / 2, fb->height() / 2);
    CHECK(c.r > 0.18f);
    CHECK(c.b > 0.24f);
    CHECK(c.g < std::max(c.r, c.b));
    CHECK(std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b}) > 0.18f);
}

TEST_CASE("GlowTorture: luminance threshold blooms only bright sources") {
    auto fb = render_glow_comp([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.005f, 0.005f, 0.012f, 1.0f});
        });

        auto box = [&](const char* name, float x, Color source) {
            s.layer(name, [=](LayerBuilder& l) {
                GlowParams g = GlowPresets::neon_blue(34.0f);
                g.threshold = 0.80f;
                g.intensity = 1.0f;
                g.additive = false;
                l.position({x, 0.0f, 0.0f}).glow(g);
                l.rect("r", {
                    .size = {34.0f, 34.0f},
                    .color = source,
                    .pos = {0.0f, 0.0f, 0.0f}
                });
            });
        };

        box("dim", -62.0f, Color{0.45f, 0.45f, 0.45f, 1.0f});
        box("hdr", 62.0f, Color{2.0f, 2.0f, 2.0f, 1.0f});
        return s.build();
    }, 260, 140);

    REQUIRE(fb != nullptr);
    const int cy = fb->height() / 2;
    const Color dim_halo = fb->get_pixel(fb->width() / 2 - 62 + 42, cy);
    const Color hdr_halo = fb->get_pixel(fb->width() / 2 + 62 + 42, cy);

    CHECK(hdr_halo.b > dim_halo.b * 3.0f);
    CHECK(hdr_halo.b > 0.025f);
}

TEST_CASE("GlowTorture: subpixel movement advances smoothly") {
    auto make_scene = [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.006f, 0.008f, 0.018f, 1.0f});
        });

        const float x = static_cast<float>(ctx.frame) * 0.15f;
        s.layer("moving", [x](LayerBuilder& l) {
            l.position({x, 0.0f, 0.0f}).glow(26.0f, 0.9f, Color{0.20f, 0.80f, 1.0f, 1.0f});
            l.circle("c", {
                .radius = 18.0f,
                .color = Color{1.0f, 1.0f, 1.0f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });
        return s.build();
    };

    auto f0 = render_glow_comp(make_scene, 220, 140, 0, 9);
    auto f4 = render_glow_comp(make_scene, 220, 140, 4, 9);
    auto f8 = render_glow_comp(make_scene, 220, 140, 8, 9);
    REQUIRE(f0 != nullptr);
    REQUIRE(f4 != nullptr);
    REQUIRE(f8 != nullptr);

    const float bg = luma(Color{0.006f, 0.008f, 0.018f, 1.0f});
    const float d04 = weighted_centroid_x(*f4, bg) - weighted_centroid_x(*f0, bg);
    const float d48 = weighted_centroid_x(*f8, bg) - weighted_centroid_x(*f4, bg);

    CHECK(d04 > 0.20f);
    CHECK(d04 < 1.00f);
    CHECK(std::abs(d04 - d48) < 0.25f);
}

TEST_CASE("GlowTorture: warm white UI card reads as light, not fog") {
    auto fb = render_glow_comp([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.003f, 0.003f, 0.006f, 1.0f});
        });

        s.layer("card", [](LayerBuilder& l) {
            GlowParams g = GlowPresets::cinematic_gold_premium(42.0f);
            g.intensity = 0.42f;
            g.color = Color{1.0f, 0.82f, 0.58f, 1.0f};
            g.additive = false;
            l.glow(g);
            l.rounded_rect("panel", {
                .size = {134.0f, 78.0f},
                .radius = 18.0f,
                .color = Color{0.96f, 0.95f, 0.91f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });
        return s.build();
    }, 260, 160);

    REQUIRE(fb != nullptr);
    const int cx = fb->width() / 2;
    const int cy = fb->height() / 2;
    const Color center = fb->get_pixel(cx, cy);
    const Color near_pixel = fb->get_pixel(cx + 88, cy);
    const Color far_pixel = fb->get_pixel(cx + 124, cy);

    CHECK(center.r > 0.90f);
    CHECK(center.g > 0.88f);
    CHECK(center.b > 0.80f);
    CHECK(near_pixel.r > far_pixel.r * 2.0f);
    CHECK(far_pixel.r < 0.08f);
}
