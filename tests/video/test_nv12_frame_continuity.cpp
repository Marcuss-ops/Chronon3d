#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/media/video/nv12_frame_continuity.hpp>

#include <vector>

using chronon3d::media::video::ContinuityConversionScope;
using chronon3d::media::video::Nv12FrameContinuity;
using chronon3d::raster::BBox;

namespace {

const std::uint32_t kW = 1920;
const std::uint32_t kH = 1080;

int buffer_token_storage = 0;
const void* kBufferA = &buffer_token_storage;

int other_buffer_token_storage = 0;
const void* kBufferB = &other_buffer_token_storage;

// BBox is a plain aggregate with no operator==: compare the canonical fields.
bool same(const BBox& a, const BBox& b) {
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

bool is_whole_frame(const ContinuityConversionScope& scope, std::uint32_t w,
                    std::uint32_t h) {
    return scope.full_frame &&
           !scope.destination_retains_previous_frame;
}

} // namespace

TEST_CASE("frame continuity: an unknown buffer is always converted whole") {
    Nv12FrameContinuity tracker;
    tracker.note_frame_damage(0, std::vector<BBox>{{10, 10, 20, 20}});
    const auto scope = tracker.plan_conversion(kBufferA, 0, kW, kH);
    CHECK(is_whole_frame(scope, kW, kH));
    CHECK(scope.regions.empty());
}

TEST_CASE("frame continuity: proven continuity narrows to the damage union") {
    Nv12FrameContinuity tracker;
    // Frame 0 wrote buffer A whole (first use).
    tracker.note_frame_damage(0, std::vector<BBox>{{0, 0, 100, 100}});
    tracker.commit(kBufferA, 0);
    // Frames 1..2 changed two small regions; buffer A is reused at frame 3.
    tracker.note_frame_damage(1, std::vector<BBox>{{10, 10, 20, 20}});
    tracker.note_frame_damage(2, std::vector<BBox>{{30, 30, 40, 40}});
    tracker.note_frame_damage(3, std::vector<BBox>{{50, 50, 60, 60}});

    const auto scope = tracker.plan_conversion(kBufferA, 3, kW, kH);
    CHECK_FALSE(scope.full_frame);
    CHECK(scope.destination_retains_previous_frame);
    REQUIRE(scope.regions.size() == 3);
    CHECK(same(scope.regions[0], BBox{10, 10, 20, 20}));
    CHECK(same(scope.regions[1], BBox{30, 30, 40, 40}));
    CHECK(same(scope.regions[2], BBox{50, 50, 60, 60}));
}

TEST_CASE("frame continuity: a gap in the damage log is unknown damage") {
    Nv12FrameContinuity tracker;
    tracker.note_frame_damage(0, std::vector<BBox>{{0, 0, 10, 10}});
    tracker.commit(kBufferA, 0);
    tracker.note_frame_damage(1, std::vector<BBox>{{0, 0, 10, 10}});
    // Frame 2 is never noted (e.g. a failed note or a lane that skipped it).
    tracker.note_frame_damage(3, std::vector<BBox>{{0, 0, 10, 10}});

    const auto scope = tracker.plan_conversion(kBufferA, 3, kW, kH);
    CHECK(is_whole_frame(scope, kW, kH));
}

TEST_CASE("frame continuity: an empty damage note is unknown, not no-change") {
    Nv12FrameContinuity tracker;
    tracker.note_frame_damage(0, std::vector<BBox>{{0, 0, 10, 10}});
    tracker.commit(kBufferA, 0);
    tracker.note_frame_damage(1, {});

    const auto scope = tracker.plan_conversion(kBufferA, 1, kW, kH);
    CHECK(is_whole_frame(scope, kW, kH));
}

TEST_CASE("frame continuity: an unchanged frame contributes no damage") {
    Nv12FrameContinuity tracker;
    tracker.note_frame_damage(0, std::vector<BBox>{{0, 0, 10, 10}});
    tracker.commit(kBufferA, 0);
    tracker.note_unchanged_frame(1);
    tracker.note_frame_damage(2, std::vector<BBox>{{100, 100, 110, 110}});

    const auto scope = tracker.plan_conversion(kBufferA, 2, kW, kH);
    CHECK_FALSE(scope.full_frame);
    CHECK(scope.destination_retains_previous_frame);
    REQUIRE(scope.regions.size() == 1);
    CHECK(same(scope.regions[0], BBox{100, 100, 110, 110}));
}

TEST_CASE("frame continuity: full-frame damage widens the plan") {
    Nv12FrameContinuity tracker;
    tracker.note_frame_damage(0, std::vector<BBox>{{0, 0, 10, 10}});
    tracker.commit(kBufferA, 0);
    tracker.note_frame_damage(
        1, std::vector<BBox>{{0, 0, static_cast<int>(kW), static_cast<int>(kH)}});
    tracker.note_frame_damage(
        2, std::vector<BBox>{{0, 0, static_cast<int>(kW), static_cast<int>(kH)}});
    tracker.note_frame_damage(
        3, std::vector<BBox>{{0, 0, static_cast<int>(kW), static_cast<int>(kH)}});

    // The accumulated union covers well over half the canvas: whole frame.
    const auto scope = tracker.plan_conversion(kBufferA, 3, kW, kH);
    CHECK(is_whole_frame(scope, kW, kH));
}

TEST_CASE("frame continuity: many micro regions collapse to one launch") {
    Nv12FrameContinuity tracker;
    tracker.note_frame_damage(0, std::vector<BBox>{{0, 0, 10, 10}});
    tracker.commit(kBufferA, 0);
    // 20 tiny regions in one frame: above kMaxRegions, below half the canvas.
    std::vector<BBox> many;
    for (int i = 0; i < 20; ++i) {
        many.push_back(BBox{i * 20, 0, i * 20 + 10, 10});
    }
    tracker.note_frame_damage(1, many);

    const auto scope = tracker.plan_conversion(kBufferA, 1, kW, kH);
    CHECK_FALSE(scope.full_frame);
    CHECK(scope.destination_retains_previous_frame);
    REQUIRE(scope.regions.size() == 1);
    CHECK(same(scope.regions[0], BBox{0, 0, 390, 10}));
}

TEST_CASE("frame continuity: commit advances the held frame") {
    Nv12FrameContinuity tracker;
    tracker.note_frame_damage(0, std::vector<BBox>{{0, 0, 10, 10}});
    tracker.commit(kBufferA, 0);
    tracker.note_frame_damage(1, std::vector<BBox>{{20, 20, 30, 30}});
    tracker.commit(kBufferA, 1);
    tracker.note_frame_damage(2, std::vector<BBox>{{40, 40, 50, 50}});

    // Only frame 2's damage is outside the held frame now.
    const auto scope = tracker.plan_conversion(kBufferA, 2, kW, kH);
    CHECK_FALSE(scope.full_frame);
    REQUIRE(scope.regions.size() == 1);
    CHECK(same(scope.regions[0], BBox{40, 40, 50, 50}));
}

TEST_CASE("frame continuity: invalidate drops the continuity claim") {
    Nv12FrameContinuity tracker;
    tracker.note_frame_damage(0, std::vector<BBox>{{0, 0, 10, 10}});
    tracker.commit(kBufferA, 0);
    tracker.note_frame_damage(1, std::vector<BBox>{{20, 20, 30, 30}});
    tracker.invalidate(kBufferA);

    const auto scope = tracker.plan_conversion(kBufferA, 1, kW, kH);
    CHECK(is_whole_frame(scope, kW, kH));
}

TEST_CASE("frame continuity: out-of-order notes are refused") {
    Nv12FrameContinuity tracker;
    tracker.note_frame_damage(5, std::vector<BBox>{{0, 0, 10, 10}});
    tracker.note_frame_damage(3, std::vector<BBox>{{0, 0, 10, 10}});
    tracker.commit(kBufferA, 5);
    tracker.note_frame_damage(6, std::vector<BBox>{{20, 20, 30, 30}});

    // The refused note for frame 3 did not corrupt the ordered log; frames
    // 5..6 are contiguous so continuity is still provable.
    const auto scope = tracker.plan_conversion(kBufferA, 6, kW, kH);
    CHECK_FALSE(scope.full_frame);
    REQUIRE(scope.regions.size() == 1);
    CHECK(same(scope.regions[0], BBox{20, 20, 30, 30}));
}

TEST_CASE("frame continuity: independent buffers keep independent claims") {
    Nv12FrameContinuity tracker;
    tracker.note_frame_damage(0, std::vector<BBox>{{0, 0, 10, 10}});
    tracker.commit(kBufferA, 0);
    tracker.commit(kBufferB, 0);
    tracker.note_frame_damage(1, std::vector<BBox>{{20, 20, 30, 30}});

    // Both buffers hold frame 0; the same damage plan applies to both.
    for (const void* buffer : {kBufferA, kBufferB}) {
        const auto scope = tracker.plan_conversion(buffer, 1, kW, kH);
        CHECK_FALSE(scope.full_frame);
        CHECK(scope.destination_retains_previous_frame);
        REQUIRE(scope.regions.size() == 1);
        CHECK(same(scope.regions[0], BBox{20, 20, 30, 30}));
    }
    CHECK(tracker.tracked_buffer_count() == 2);
}

TEST_CASE("frame continuity: history trim degrades to whole-frame, never tears") {
    Nv12FrameContinuity tracker(/*history_depth=*/2);
    tracker.note_frame_damage(0, std::vector<BBox>{{0, 0, 10, 10}});
    tracker.commit(kBufferA, 0);
    tracker.note_frame_damage(1, std::vector<BBox>{{0, 0, 10, 10}});
    tracker.note_frame_damage(2, std::vector<BBox>{{0, 0, 10, 10}});
    tracker.note_frame_damage(3, std::vector<BBox>{{0, 0, 10, 10}});

    // Frame 0's entry has been trimmed; the (0, 3] walk cannot be proven.
    const auto scope = tracker.plan_conversion(kBufferA, 3, kW, kH);
    CHECK(is_whole_frame(scope, kW, kH));
}

TEST_CASE("frame continuity: reset forgets every claim") {
    Nv12FrameContinuity tracker;
    tracker.note_frame_damage(0, std::vector<BBox>{{0, 0, 10, 10}});
    tracker.commit(kBufferA, 0);
    tracker.reset();
    CHECK(tracker.tracked_buffer_count() == 0);

    tracker.note_frame_damage(1, std::vector<BBox>{{20, 20, 30, 30}});
    const auto scope = tracker.plan_conversion(kBufferA, 1, kW, kH);
    CHECK(is_whole_frame(scope, kW, kH));
}
