#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <tests/helpers/render_fixtures.hpp>
#include <tests/helpers/test_utils.hpp>

#include <xxhash.h>

using namespace chronon3d;
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

} // namespace

TEST_CASE("Crossfade transition_in renders progressive opacity") {
    auto renderer = make_renderer();
    
    LayerTransitionSpec trans_in{
        .transition_id = "crossfade",
        .duration = 1.0, // 30 frames at 30 fps
        .delay = 0.0,
        .easing = Easing::Linear
    };
    
    auto comp = make_transition_comp(trans_in, {});

    // Frame 0: beginning of transition. Progress = 0.0 => layer fully transparent => black bg
    auto fb0 = renderer.render_frame(comp, 0);
    REQUIRE(fb0 != nullptr);
    CHECK(fb0->get_pixel(50, 50).r == 0.0f);
    CHECK(fb0->get_pixel(50, 50).a == 1.0f); // background is black, opaque

    // Frame 15: middle of transition. Progress = 0.5 => layer half transparent
    auto fb15 = renderer.render_frame(comp, 15);
    REQUIRE(fb15 != nullptr);
    CHECK(fb15->get_pixel(50, 50).r > 0.4f);
    CHECK(fb15->get_pixel(50, 50).r < 0.6f);

    // Frame 30: end of transition. Progress = 1.0 => layer fully visible
    auto fb30 = renderer.render_frame(comp, 30);
    REQUIRE(fb30 != nullptr);
    CHECK(fb30->get_pixel(50, 50).r == 1.0f);
}

TEST_CASE("Transition direction and different types execute successfully") {
    auto renderer = make_renderer();

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
        auto fb0 = renderer.render_frame(comp, 0);
        auto fb15 = renderer.render_frame(comp, 15);
        auto fb30 = renderer.render_frame(comp, 30);

        REQUIRE(fb0 != nullptr);
        REQUIRE(fb15 != nullptr);
        REQUIRE(fb30 != nullptr);

        CHECK(framebuffer_hash(*fb0) != framebuffer_hash(*fb15));
        CHECK(framebuffer_hash(*fb15) != framebuffer_hash(*fb30));
    }
}

TEST_CASE("Remotion-style transitions compute noise-driven masks and colors") {
    auto renderer = make_renderer();

    // Verify procedural_remotion
    {
        LayerTransitionSpec trans_in{
            .transition_id = "procedural_remotion",
            .duration = 1.0,
            .delay = 0.0,
            .easing = Easing::Linear
        };
        auto comp = make_transition_comp(trans_in, {});
        
        auto fb0 = renderer.render_frame(comp, 0);
        auto fb15 = renderer.render_frame(comp, 15);
        auto fb30 = renderer.render_frame(comp, 30);
        
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
        
        auto fb0 = renderer.render_frame(comp, 0);
        auto fb15 = renderer.render_frame(comp, 15);
        auto fb30 = renderer.render_frame(comp, 30);
        
        REQUIRE(fb0 != nullptr);
        REQUIRE(fb15 != nullptr);
        REQUIRE(fb30 != nullptr);
        
        CHECK(framebuffer_hash(*fb0) != framebuffer_hash(*fb15));
        CHECK(framebuffer_hash(*fb15) != framebuffer_hash(*fb30));
    }
}

