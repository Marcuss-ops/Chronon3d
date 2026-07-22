#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/transition/transition_catalog.hpp>
#include <tests/helpers/render_fixtures.hpp>
#include <tests/helpers/test_utils.hpp>

#include <xxhash.h>
#include <stdexcept>

using namespace chronon3d;
using namespace chronon3d::graph;
using namespace chronon3d::test;

namespace {

Composition make_transition_comp(
    LayerTransitionSpec trans_in,
    LayerTransitionSpec trans_out,
    Frame duration = 30
) {
    return composition(CompositionSpec{
        .name = "TransitionTest",
        .width = 100,
        .height = 100,
        .frame_rate = FrameRate{30, 1},
        .duration = duration
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color::black());
        });

        s.layer("trans_layer", [=](LayerBuilder& l) {
            l.transition_in(trans_in);
            l.transition_out(trans_out);
            l.rect("red_rect", {
                .size = {80, 80},
                .color = Color::red(),
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });
}

LayerTransitionSpec spec(const char* id, float duration_seconds = 1.0f,
                         Easing easing = Easing::Linear) {
    LayerTransitionSpec s;
    s.transition_id = id;
    s.duration = duration_seconds;
    s.delay = 0.0;
    s.easing = easing;
    return s;
}

} // namespace

TEST_CASE("Crossfade transition_in renders progressive opacity") {
    auto renderer = test::make_renderer();
    
    LayerTransitionSpec trans_in{
        .transition_id = "crossfade",
        .duration = 1.0, // 30 frames at 30 fps
        .delay = 0.0,
        .easing = Easing::Linear
    };
    
    auto comp = make_transition_comp(trans_in, {});

    // Frame 0: beginning of transition. Progress = 0.0 => layer fully transparent => black bg
    auto fb0 = renderer.render(comp, 0);
    REQUIRE(fb0 != nullptr);
    CHECK(fb0->get_pixel(50, 50).r == 0.0f);
    CHECK(fb0->get_pixel(50, 50).a == 1.0f); // background is black, opaque

    // Frame 15: middle of transition. Progress = 0.5 => layer half transparent
    auto fb15 = renderer.render(comp, 15);
    REQUIRE(fb15 != nullptr);
    CHECK(fb15->get_pixel(50, 50).r > 0.4f);
    CHECK(fb15->get_pixel(50, 50).r < 0.6f);

    // Frame 30: end of transition. Progress = 1.0 => layer fully visible
    auto fb30 = renderer.render(comp, 30);
    REQUIRE(fb30 != nullptr);
    CHECK(fb30->get_pixel(50, 50).r == 1.0f);
}

TEST_CASE("Transition direction and different types execute successfully") {
    auto renderer = test::make_renderer();

    std::vector<std::string> transition_types = {
        "slide", "wipe_linear", "smooth_wipe", "circle_iris", "flash", "procedural_remotion", "remotion"
    };

    for (const auto& type : transition_types) {
        LayerTransitionSpec trans_in{
            .transition_id = type,
            .direction = TransitionDirection::Right,
            .duration = 1.0,
            .delay = 0.0,
            .easing = Easing::Linear
        };
        auto comp = make_transition_comp(trans_in, {});

        // Just ensure it renders without crashing and middle state is distinct from end state
        auto fb0 = renderer.render(comp, 0);
        auto fb15 = renderer.render(comp, 15);
        auto fb30 = renderer.render(comp, 30);

        REQUIRE(fb0 != nullptr);
        REQUIRE(fb15 != nullptr);
        REQUIRE(fb30 != nullptr);

        CHECK(framebuffer_hash(*fb0) != framebuffer_hash(*fb15));
        CHECK(framebuffer_hash(*fb15) != framebuffer_hash(*fb30));
    }
}

TEST_CASE("Transition in and out coexist on the same layer") {
    auto renderer = test::make_renderer();

    LayerTransitionSpec trans_in{
        .transition_id = "crossfade",
        .duration = 0.5,  // 15 frames at 30 fps
        .delay = 0.0,
        .easing = Easing::Linear
    };
    LayerTransitionSpec trans_out{
        .transition_id = "crossfade",
        .duration = 0.5,
        .delay = 0.0,
        .easing = Easing::Linear
    };

    auto comp = make_transition_comp(trans_in, trans_out, 60);

    // Layer fully visible in the middle (after in, before out).
    auto fb_mid = renderer.render(comp, 30);
    REQUIRE(fb_mid != nullptr);
    CHECK(fb_mid->get_pixel(50, 50).r == doctest::Approx(1.0f));

    // Layer fading in at the start.
    auto fb_start = renderer.render(comp, 7);
    REQUIRE(fb_start != nullptr);
    CHECK(fb_start->get_pixel(50, 50).r > 0.0f);
    CHECK(fb_start->get_pixel(50, 50).r < fb_mid->get_pixel(50, 50).r);

    // Layer fading out at the end.
    auto fb_end = renderer.render(comp, 53);
    REQUIRE(fb_end != nullptr);
    CHECK(fb_end->get_pixel(50, 50).r < fb_mid->get_pixel(50, 50).r);
}

TEST_CASE("Unknown transition id fails loudly") {
    auto renderer = test::make_renderer();

    LayerTransitionSpec trans_in{
        .transition_id = "typo_transition",
        .duration = 1.0,
        .delay = 0.0,
        .easing = Easing::Linear
    };
    auto comp = make_transition_comp(trans_in, {});
    REQUIRE_THROWS(renderer.render(comp, 0));
}

TEST_CASE("Typed parameters affect transition output") {
    auto renderer = test::make_renderer();

    LayerTransitionSpec trans_short = spec("slide", 1.0f);
    trans_short.parameters = SlideParams{.distance = 0.5f};
    LayerTransitionSpec trans_long = spec("slide", 1.0f);
    trans_long.parameters = SlideParams{.distance = 2.0f};

    auto comp_short = make_transition_comp(trans_short, {}, 60);
    auto comp_long = make_transition_comp(trans_long, {}, 60);

    auto fb_short = renderer.render(comp_short, 15);
    auto fb_long = renderer.render(comp_long, 15);

    REQUIRE(fb_short != nullptr);
    REQUIRE(fb_long != nullptr);
    CHECK(framebuffer_hash(*fb_short) != framebuffer_hash(*fb_long));
}

TEST_CASE("Cache key includes typed parameters") {
    LayerTransitionSpec a = spec("slide", 1.0f);
    a.parameters = SlideParams{.distance = 0.5f};
    LayerTransitionSpec b = spec("slide", 1.0f);
    b.parameters = SlideParams{.distance = 2.0f};

    CHECK(hash_layer_transition_spec(a) != hash_layer_transition_spec(b));

    LayerTransitionSpec c = spec("smooth_wipe", 1.0f);
    c.parameters = SmoothWipeParams{.feather = 0.05f};
    LayerTransitionSpec d = spec("smooth_wipe", 1.0f);
    d.parameters = SmoothWipeParams{.feather = 0.20f};

    CHECK(hash_layer_transition_spec(c) != hash_layer_transition_spec(d));
}

TEST_CASE("LayerTransitionCatalog rejects unknown ids") {
    LayerTransitionCatalog catalog;
    LayerTransitionCatalog::register_builtin(catalog);

    LayerTransitionSpec known = spec("crossfade");
    CHECK(catalog.contains("crossfade"));
    CHECK(catalog.resolve(known) != nullptr);

    LayerTransitionSpec unknown = spec("does_not_exist");
    CHECK(!catalog.contains("does_not_exist"));
    CHECK_THROWS(catalog.resolve(unknown));
}

TEST_CASE("Remotion-style transitions compute noise-driven masks and colors") {
    auto renderer = test::make_renderer();

    // Verify procedural_remotion
    {
        LayerTransitionSpec trans_in{
            .transition_id = "procedural_remotion",
            .duration = 1.0,
            .delay = 0.0,
            .easing = Easing::Linear
        };
        auto comp = make_transition_comp(trans_in, {});
        
        auto fb0 = renderer.render(comp, 0);
        auto fb15 = renderer.render(comp, 15);
        auto fb30 = renderer.render(comp, 30);
        
        REQUIRE(fb0 != nullptr);
        REQUIRE(fb15 != nullptr);
        REQUIRE(fb30 != nullptr);
        
        CHECK(framebuffer_hash(*fb0) != framebuffer_hash(*fb15));
        CHECK(framebuffer_hash(*fb15) != framebuffer_hash(*fb30));
    }

    // Verify remotion
    {
        LayerTransitionSpec trans_in{
            .transition_id = "remotion",
            .duration = 1.0,
            .delay = 0.0,
            .easing = Easing::Linear
        };
        auto comp = make_transition_comp(trans_in, {});
        
        auto fb0 = renderer.render(comp, 0);
        auto fb15 = renderer.render(comp, 15);
        auto fb30 = renderer.render(comp, 30);
        
        REQUIRE(fb0 != nullptr);
        REQUIRE(fb15 != nullptr);
        REQUIRE(fb30 != nullptr);
        
        CHECK(framebuffer_hash(*fb0) != framebuffer_hash(*fb15));
        CHECK(framebuffer_hash(*fb15) != framebuffer_hash(*fb30));
    }
}

