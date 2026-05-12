#include <doctest/doctest.h>
#include <chronon3d/timeline/sequence.hpp>

using namespace chronon3d;

TEST_CASE("Sequence logic") {
    FrameContext ctx;
    ctx.frame = Frame{50};
    ctx.frame_rate = {30, 1};

    SUBCASE("Before sequence") {
        auto seq = sequence(ctx, Frame{60}, Frame{30});
        CHECK(seq.active == false);
        CHECK(seq.frame == 0);
        CHECK(seq.progress() == 0.0f);
    }

    SUBCASE("Start of sequence") {
        ctx.frame = Frame{60};
        auto seq = sequence(ctx, Frame{60}, Frame{30});
        CHECK(seq.active == true);
        CHECK(seq.frame == 0);
        CHECK(seq.progress() == 0.0f);
    }

    SUBCASE("Middle of sequence") {
        ctx.frame = Frame{75};
        auto seq = sequence(ctx, Frame{60}, Frame{30});
        CHECK(seq.active == true);
        CHECK(seq.frame == 15);
        CHECK(seq.progress() == 0.5f);
        CHECK(seq.seconds() == 0.5f);
    }

    SUBCASE("End of sequence (exclusive)") {
        ctx.frame = Frame{90};
        auto seq = sequence(ctx, Frame{60}, Frame{30});
        CHECK(seq.active == false);
    }
}
