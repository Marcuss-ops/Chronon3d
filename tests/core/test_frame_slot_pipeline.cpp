#include <doctest/doctest.h>

#include <chronon3d/runtime/frame_slot_pipeline.hpp>

TEST_CASE("FrameSlotPipeline enforces bounded three-stage ownership") {
    chronon3d::runtime::FrameSlotPipeline<3> pipeline;
    CHECK(pipeline.depth() == 3);

    auto* evaluated = pipeline.acquire_for_evaluation();
    REQUIRE(evaluated != nullptr);
    evaluated->frame = chronon3d::Frame{12};
    evaluated->sequence = 12;
    CHECK(pipeline.publish_evaluated(*evaluated));
    CHECK(evaluated->state == chronon3d::runtime::FrameSlotState::Evaluated);

    auto* rendered = pipeline.acquire_for_render();
    REQUIRE(rendered == evaluated);
    CHECK(pipeline.publish_rendered(*rendered));
    CHECK(rendered->state == chronon3d::runtime::FrameSlotState::Rendered);

    auto* encoding = pipeline.acquire_for_encoding();
    REQUIRE(encoding == rendered);
    CHECK(pipeline.begin_encoding(*encoding));
    CHECK(encoding->state == chronon3d::runtime::FrameSlotState::Encoding);
    CHECK(pipeline.release_encoded(*encoding));
    CHECK(encoding->state == chronon3d::runtime::FrameSlotState::Free);
    CHECK(encoding->frame == chronon3d::Frame{-1});
}

TEST_CASE("FrameSlotPipeline refuses a fourth in-flight frame") {
    chronon3d::runtime::FrameSlotPipeline<3> pipeline;
    auto* a = pipeline.acquire_for_evaluation();
    auto* b = pipeline.acquire_for_evaluation();
    auto* c = pipeline.acquire_for_evaluation();
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    CHECK(pipeline.acquire_for_evaluation() == nullptr);
}

TEST_CASE("FrameSlotPipeline rejects invalid state transitions") {
    chronon3d::runtime::FrameSlotPipeline<3> pipeline;
    auto* slot = pipeline.acquire_for_evaluation();
    REQUIRE(slot != nullptr);
    CHECK_FALSE(pipeline.publish_rendered(*slot));
    CHECK(slot->state == chronon3d::runtime::FrameSlotState::Evaluating);
}
