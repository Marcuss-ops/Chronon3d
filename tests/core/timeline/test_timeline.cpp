#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

TEST_CASE("Code-first Composition") {
    CompositionSpec spec;
    spec.name = "TestComp";
    spec.duration = 100;

    Composition comp{
        spec,
        [](const FrameContext& ctx) {
            SceneBuilder builder(ctx.resource);
            auto x = interpolate(ctx.frame, 0, 100, 0.0f, 100.0f);
            builder.rect("box", {.size={100, 100}, .color=Color::white(), .pos={x, 0, 0}});
            return builder.build();
        }
    };

    SUBCASE("Evaluation at frame 0") {
        Scene s = comp.evaluate(0);
        CHECK(s.nodes().size() == 1);
        CHECK(s.nodes()[0].world_transform.position.x == 0.0f);
    }

    SUBCASE("Evaluation at frame 50") {
        Scene s = comp.evaluate(50);
        CHECK(s.nodes().size() == 1);
        CHECK(s.nodes()[0].world_transform.position.x == 50.0f);
    }

    SUBCASE("Evaluation at frame 100") {
        Scene s = comp.evaluate(100);
        CHECK(s.nodes().size() == 1);
        CHECK(s.nodes()[0].world_transform.position.x == 100.0f);
    }
}

TEST_CASE("Interpolation helper") {
    CHECK(interpolate(50.0f, 0.0f, 100.0f, 0.0f, 1.0f) == doctest::Approx(0.5f));
    CHECK(interpolate(0.0f, 0.0f, 100.0f, 0.0f, 1.0f) == doctest::Approx(0.0f));
    CHECK(interpolate(100.0f, 0.0f, 100.0f, 0.0f, 1.0f) == doctest::Approx(1.0f));
    CHECK(interpolate(150.0f, 0.0f, 100.0f, 0.0f, 1.0f) == doctest::Approx(1.0f)); // Clamped
}
