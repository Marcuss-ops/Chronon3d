#include <doctest/doctest.h>
#include <xxhash.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/presets/text_box.hpp>
#include <chronon3d/presets/youtube_text.hpp>
#include <tests/helpers/render_fixtures.hpp>

using namespace chronon3d;
using namespace chronon3d::presets;
using namespace chronon3d::test;

namespace {

u64 framebuffer_hash(const Framebuffer& fb) {
    return XXH64(fb.pixels_row(0), fb.size_bytes(), 0);
}

Composition make_text_box_comp(TextBoxParams params) {
    return composition({
        .name = "TextBoxTest", .width = 640, .height = 360, .duration = 1
    }, [params](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("box", [params](LayerBuilder& l) {
            l.position({0, 0, 0});
            text_box(l, "tb", params);
        });
        return s.build();
    });
}

} // namespace

TEST_CASE("TextBox: background + text renders non-blank") {
    TextBoxParams p;
    p.text             = "Hello Chronon3d";
    p.style.size       = {400.0f, 80.0f};
    p.style.background_color = {0.1f, 0.1f, 0.1f, 0.9f};
    p.style.shadow_enabled   = false;

    auto fb = render_modular(make_text_box_comp(p));
    REQUIRE(fb != nullptr);
    save_debug(*fb, "output/debug/text_box/01_basic.png");

    bool has_opaque = false;
    for (int y = 0; y < fb->height() && !has_opaque; ++y)
        for (int x = 0; x < fb->width() && !has_opaque; ++x)
            if (fb->get_pixel(x, y).a > 0.5f) has_opaque = true;
    CHECK(has_opaque);
}

TEST_CASE("TextBox: background disabled renders no background") {
    TextBoxParams with_bg, without_bg;
    with_bg.text    = without_bg.text    = "Test";
    with_bg.style.size = without_bg.style.size = {400.0f, 80.0f};
    with_bg.style.shadow_enabled   = without_bg.style.shadow_enabled   = false;
    with_bg.style.background_enabled = true;
    without_bg.style.background_enabled = false;

    auto fb_with    = render_modular(make_text_box_comp(with_bg));
    auto fb_without = render_modular(make_text_box_comp(without_bg));
    REQUIRE(fb_with != nullptr);
    REQUIRE(fb_without != nullptr);

    save_debug(*fb_with,    "output/debug/text_box/02_with_bg.png");
    save_debug(*fb_without, "output/debug/text_box/03_no_bg.png");

    CHECK(framebuffer_hash(*fb_with) != framebuffer_hash(*fb_without));
}

TEST_CASE("TextBox: different background colors produce different output") {
    // Uses background color difference so the test is font-independent.
    TextBoxParams p1, p2;
    p1.text = p2.text = "Same Text";
    p1.style.size = p2.style.size = {400.0f, 80.0f};
    p1.style.shadow_enabled = p2.style.shadow_enabled = false;
    p1.style.background_color = {0.1f, 0.1f, 0.1f, 0.9f};
    p2.style.background_color = {0.9f, 0.2f, 0.2f, 0.9f};

    auto fb1 = render_modular(make_text_box_comp(p1));
    auto fb2 = render_modular(make_text_box_comp(p2));
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);

    save_debug(*fb1, "output/debug/text_box/06_dark_bg.png");
    save_debug(*fb2, "output/debug/text_box/07_red_bg.png");

    CHECK(framebuffer_hash(*fb1) != framebuffer_hash(*fb2));
}

TEST_CASE("YouTube: title_card renders non-blank") {
    auto comp = composition({
        .name = "YouTubeTitle", .width = 640, .height = 360, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("title", [](LayerBuilder& l) {
            l.position({0, 0, 0});
            youtube::title_card(l, "My YouTube Video Title");
        });
        return s.build();
    });

    auto fb = render_modular(comp);
    REQUIRE(fb != nullptr);
    save_debug(*fb, "output/debug/text_box/04_youtube_title.png");

    bool has_opaque = false;
    for (int y = 0; y < fb->height() && !has_opaque; ++y)
        for (int x = 0; x < fb->width() && !has_opaque; ++x)
            if (fb->get_pixel(x, y).a > 0.5f) has_opaque = true;
    CHECK(has_opaque);
}

TEST_CASE("YouTube: lower_third renders non-blank") {
    auto comp = composition({
        .name = "LowerThird", .width = 640, .height = 360, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("lower3rd", [](LayerBuilder& l) {
            l.position({0, 100, 0});
            youtube::lower_third(l, "John Doe — CEO");
        });
        return s.build();
    });

    auto fb = render_modular(comp);
    REQUIRE(fb != nullptr);
    save_debug(*fb, "output/debug/text_box/05_lower_third.png");

    bool has_opaque = false;
    for (int y = 0; y < fb->height() && !has_opaque; ++y)
        for (int x = 0; x < fb->width() && !has_opaque; ++x)
            if (fb->get_pixel(x, y).a > 0.5f) has_opaque = true;
    CHECK(has_opaque);
}
