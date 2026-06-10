#include <doctest/doctest.h>
#include <chronon3d/timeline/composition.hpp>

using namespace chronon3d;

TEST_CASE("Composition foundation") {
    CompositionSpec spec{
        .name = "TestComp",
        .width = 1280,
        .height = 720,
        .frame_rate = {30, 1},
        .duration = 300
    };

    SUBCASE("Property retention") {
        Composition comp(spec, [](const FrameContext&){ return Scene{}; });
        CHECK(comp.name() == "TestComp");
        CHECK(comp.width() == 1280);
        CHECK(comp.height() == 720);
        CHECK(comp.duration() == 300);
        CHECK(comp.frame_rate().numerator == 30);
    }

    SUBCASE("Evaluation and context passing") {
        bool called = false;
        Composition comp(spec, [&](const FrameContext& ctx) {
            called = true;
            CHECK(ctx.frame == 150);
            CHECK(ctx.duration == 300);
            return Scene{};
        });

        auto scene = comp.evaluate(150);
        CHECK(called);
    }
}
