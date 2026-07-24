#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/nodes/transition_node.hpp>
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
    Frame duration = 30,
    Frame layer_duration = Frame{-1}
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
            if (layer_duration >= Frame{0}) {
                l.duration(layer_duration);
            }
            l.transition_in(trans_in);
            l.transition_out(trans_out);
            l.rect("red_rect", {
                .size = {80, 80},
                .color = Color::red(),
                .pos = {40, 40, 0}
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

    // 2-second layer: crossfade in for 0.5s, crossfade out for the last 0.5s.
    LayerTransitionSpec trans_in{
        .transition_id = "crossfade",
        .duration = 0.5,
        .delay = 0.0,
        .easing = Easing::Linear
    };
    LayerTransitionSpec trans_out{
        .transition_id = "crossfade",
        .duration = 0.5,
        .delay = 0.0,
        .easing = Easing::Linear
    };

    // layer_duration must be finite for the out transition to ever start.
    auto comp = make_transition_comp(trans_in, trans_out, /*duration=*/Frame{60}, /*layer_duration=*/Frame{60});

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
    // Use separate renderers so the composition cache cannot accidentally
    // reuse the same program for the two compositions.
    auto renderer_a = test::make_renderer();
    auto renderer_b = test::make_renderer();

    // smooth_wipe's feather changes the soft edge width, producing a
    // visibly different mask even on a uniform background.
    LayerTransitionSpec spec_a = spec("smooth_wipe", 1.0f);
    spec_a.parameters = SmoothWipeParams{.feather = 0.05f};
    LayerTransitionSpec spec_b = spec("smooth_wipe", 1.0f);
    spec_b.parameters = SmoothWipeParams{.feather = 0.50f};

    auto comp_a = make_transition_comp(spec_a, {}, Frame{60});
    auto comp_b = make_transition_comp(spec_b, {}, Frame{60});

    auto fb_a = renderer_a.render(comp_a, 15);
    auto fb_b = renderer_b.render(comp_b, 15);

    REQUIRE(fb_a != nullptr);
    REQUIRE(fb_b != nullptr);
    CHECK(framebuffer_hash(*fb_a) != framebuffer_hash(*fb_b));
}

TEST_CASE("Cache key includes duration, delay, easing and direction") {
    LayerTransitionSpec base = spec("crossfade", 1.0f);
    LayerTransitionSpec duration = base;
    duration.duration = 2.0f;
    LayerTransitionSpec delay = base;
    delay.delay = 0.5f;
    LayerTransitionSpec easing = base;
    easing.easing = Easing::OutCubic;
    LayerTransitionSpec direction = spec("slide", 1.0f);
    direction.direction = TransitionDirection::Right;
    LayerTransitionSpec direction2 = direction;
    direction2.direction = TransitionDirection::Left;

    CHECK(hash_layer_transition_spec(base) != hash_layer_transition_spec(duration));
    CHECK(hash_layer_transition_spec(base) != hash_layer_transition_spec(delay));
    CHECK(hash_layer_transition_spec(base) != hash_layer_transition_spec(easing));
    CHECK(hash_layer_transition_spec(direction) != hash_layer_transition_spec(direction2));
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

    LayerTransitionSpec e = spec("circle_iris", 1.0f);
    e.parameters = CircleIrisParams{.center = Vec2{0.5f, 0.5f}, .feather = 0.1f};
    LayerTransitionSpec f = spec("circle_iris", 1.0f);
    f.parameters = CircleIrisParams{.center = Vec2{0.25f, 0.75f}, .feather = 0.3f};
    CHECK(hash_layer_transition_spec(e) != hash_layer_transition_spec(f));

    LayerTransitionSpec g = spec("flash", 1.0f);
    g.parameters = FlashParams{.color = Color::white()};
    LayerTransitionSpec h = spec("flash", 1.0f);
    h.parameters = FlashParams{.color = Color::red()};
    CHECK(hash_layer_transition_spec(g) != hash_layer_transition_spec(h));

    LayerTransitionSpec i = spec("procedural_remotion", 1.0f);
    i.parameters = ProceduralRemotionParams{.seed = 1.2f, .inner_color = Color::white()};
    LayerTransitionSpec j = spec("procedural_remotion", 1.0f);
    j.parameters = ProceduralRemotionParams{.seed = 3.4f, .inner_color = Color::black()};
    CHECK(hash_layer_transition_spec(i) != hash_layer_transition_spec(j));

    LayerTransitionSpec k = spec("remotion", 1.0f);
    k.parameters = RemotionParams{.speed = 1.35f, .direction = 3.0f, .angle = 0.0f};
    LayerTransitionSpec l = spec("remotion", 1.0f);
    l.parameters = RemotionParams{.speed = 2.0f, .direction = 1.0f, .angle = 1.57f};
    CHECK(hash_layer_transition_spec(k) != hash_layer_transition_spec(l));
}

TEST_CASE("LayerTransitionCatalog rejects unknown ids") {
    LayerTransitionCatalog catalog;
    LayerTransitionCatalog::register_builtin(catalog);

    LayerTransitionSpec known = spec("crossfade");
    CHECK(catalog.contains("crossfade"));
    CHECK(catalog.resolve(known) != nullptr);

    LayerTransitionSpec unknown = spec("does_not_exist");
    CHECK(!catalog.contains("does_not_exist"));
    CHECK_THROWS(static_cast<void>(catalog.resolve(unknown)));
}

TEST_CASE("TransitionNode in/out split cache key") {
    LayerTransitionSpec s = spec("crossfade", 1.0f);
    TransitionNode in_node("layer", s, false, Frame{0}, Frame{30});
    TransitionNode out_node("layer", s, true, Frame{0}, Frame{30});

    // Minimal RenderGraphContext is sufficient: cache_key only reads frame_input.
    RenderGraphContext ctx;
    ctx.frame_input.frame = 0;
    ctx.frame_input.width = 100;
    ctx.frame_input.height = 100;
    ctx.frame_input.fps = 30.0f;

    auto in_key = in_node.cache_key(ctx);
    auto out_key = out_node.cache_key(ctx);

    // is_out is folded into params_hash; the rest of the key should match.
    CHECK(in_key.params_hash != out_key.params_hash);
    CHECK(in_key.scope == out_key.scope);
    CHECK(in_key.frame == out_key.frame);
    CHECK(in_key.width == out_key.width);
    CHECK(in_key.height == out_key.height);
}

TEST_CASE("Identical transition specs produce identical cache keys") {
    LayerTransitionSpec a = spec("crossfade", 1.0f);
    a.direction = TransitionDirection::Right;
    a.delay = 0.25;
    a.easing = Easing::OutQuad;
    a.parameters = SmoothWipeParams{.feather = 0.15f};

    LayerTransitionSpec b = a;

    CHECK(hash_layer_transition_spec(a) == hash_layer_transition_spec(b));
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

