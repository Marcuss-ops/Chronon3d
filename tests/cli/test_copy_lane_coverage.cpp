// =============================================================================
// test_copy_lane_coverage.cpp — the copy lane's trust boundary.
//
// The GOP copy lane publishes the compressed source's packets verbatim outside
// the declared `ChangedInterval`s, so a declaration that does not cover what the
// plan draws publishes frames without the overlay and still exits 0. These
// assertions pin the refusal for both shapes of the hole:
//
//   * `gop_copy_only` over a plan that draws anything additive;
//   * declared content intervals that do not cover an additive layer's window.
//
// They also pin what must NOT be refused (the passthrough base-only plan, a
// zero-opacity layer, an empty declaration, a union of intervals), because a
// fail-closed check that is too eager silently becomes a production outage.
// =============================================================================

#include <doctest/doctest.h>

#include <apps/chronon3d_cli/utils/video/copy_lane_coverage.hpp>

#include <chronon3d/media/video/video_execution_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace cli = chronon3d::cli;
namespace rp = chronon3d::render_plan;
using chronon3d::media::ChangedInterval;
using chronon3d::media::ChangedIntervalKind;

/// 30 fps, 300 frames = 10 s of timeline.
rp::RenderPlan make_plan(std::int64_t duration_frames = 300,
                         std::int32_t fps_num = 30,
                         std::int32_t fps_den = 1) {
    rp::RenderPlan plan;
    plan.job_id = "copy-lane-fixture";
    plan.canvas.width = 1920;
    plan.canvas.height = 1080;
    plan.canvas.fps = chronon3d::FrameRate{fps_num, fps_den};
    plan.canvas.duration = chronon3d::Frame{duration_frames};
    return plan;
}

rp::LayerPlan text_layer(std::string id, std::optional<chronon3d::Frame> start,
                         std::optional<chronon3d::Frame> duration) {
    rp::LayerPlan layer;
    layer.id = std::move(id);
    layer.type = rp::LayerType::Text;
    layer.text = "Potenza e disciplina";
    layer.start_frame = start;
    layer.duration_frames = duration;
    return layer;
}

rp::LayerPlan base_video_layer() {
    rp::LayerPlan layer;
    layer.id = "base";
    layer.type = rp::LayerType::Video;
    layer.source = "media/base.mp4";
    return layer;
}

ChangedInterval content(double from, double to) {
    return ChangedInterval{.start_seconds = from,
                           .end_seconds = to,
                           .kind = ChangedIntervalKind::Content};
}

ChangedInterval trim(double from, double to) {
    return ChangedInterval{.start_seconds = from,
                           .end_seconds = to,
                           .kind = ChangedIntervalKind::Trim};
}

/// A declaration, as the caller supplies it (a container; `std::span` is the
/// parameter type and a braced list cannot bind to it directly).
std::vector<ChangedInterval> intervals(
    std::initializer_list<ChangedInterval> declared) {
    return std::vector<ChangedInterval>{declared};
}

cli::CopyLaneCoverageVerdict coverage(
    const rp::RenderPlan& plan, bool gop_copy_only,
    std::initializer_list<ChangedInterval> declared) {
    return cli::validate_copy_lane_coverage(plan, gop_copy_only,
                                            intervals(declared));
}

} // namespace

TEST_CASE("copy lane refuses gop_copy_only over a plan that draws layers") {
    auto plan = make_plan();
    plan.layers.push_back(base_video_layer());
    plan.layers.push_back(text_layer("lower_third", chronon3d::Frame{150},
                                     chronon3d::Frame{90}));

    const auto verdict = cli::validate_copy_lane_coverage(plan, true, {});
    REQUIRE_FALSE(verdict.ok);
    // The refusal names the flag and the offending layer: it is read by a
    // producer, not by a debugger.
    CHECK(verdict.message.find("gop_copy_only") != std::string::npos);
    CHECK(verdict.message.find("lower_third") != std::string::npos);
    REQUIRE(verdict.uncovered.size() == 1);
    CHECK(verdict.uncovered.front().layer_id == "lower_third");
    CHECK(verdict.uncovered.front().first_second == doctest::Approx(5.0));
    CHECK(verdict.uncovered.front().last_second == doctest::Approx(8.0));
}

TEST_CASE("copy lane accepts gop_copy_only for the passthrough base-only plan") {
    auto plan = make_plan();
    plan.layers.push_back(base_video_layer());

    CHECK(cli::validate_copy_lane_coverage(plan, true, {}).ok);
}

TEST_CASE("copy lane refuses a second video layer: the source cannot contain it") {
    auto plan = make_plan();
    plan.layers.push_back(base_video_layer());
    auto pip = base_video_layer();
    pip.id = "pip";
    pip.source = "media/overlay.mp4";
    plan.layers.push_back(pip);

    const auto verdict = cli::validate_copy_lane_coverage(plan, true, {});
    REQUIRE_FALSE(verdict.ok);
    CHECK(verdict.message.find("pip") != std::string::npos);
}

TEST_CASE("declared content intervals must cover every additive layer window") {
    auto plan = make_plan();
    plan.layers.push_back(base_video_layer());
    plan.layers.push_back(text_layer("lower_third", chronon3d::Frame{150},
                                     chronon3d::Frame{90}));

    SUBCASE("covering declaration passes") {
        CHECK(coverage(plan, false, {content(5.0, 8.0)}).ok);
    }
    SUBCASE("a union of intervals covers a span") {
        CHECK(coverage(plan, false, {content(5.0, 6.5), content(6.5, 8.0)}).ok);
    }
    SUBCASE("a short declaration is refused, naming the gap") {
        const auto verdict = coverage(plan, false, {content(5.0, 7.0)});
        REQUIRE_FALSE(verdict.ok);
        CHECK(verdict.message.find("lower_third") != std::string::npos);
        REQUIRE(verdict.uncovered.size() == 1);
        CHECK(verdict.uncovered.front().first_second == doctest::Approx(7.0));
        CHECK(verdict.uncovered.front().last_second == doctest::Approx(8.0));
    }
    SUBCASE("a declaration that starts late is refused") {
        const auto verdict = coverage(plan, false, {content(5.5, 8.0)});
        REQUIRE_FALSE(verdict.ok);
        REQUIRE(verdict.uncovered.size() == 1);
        CHECK(verdict.uncovered.front().first_second == doctest::Approx(5.0));
        CHECK(verdict.uncovered.front().last_second == doctest::Approx(5.5));
    }
    SUBCASE("two separated declarations leave two gaps") {
        const auto verdict = coverage(plan, false, {content(5.0, 6.0), content(7.0, 7.5)});
        REQUIRE_FALSE(verdict.ok);
        REQUIRE(verdict.uncovered.size() == 2);
        CHECK(verdict.uncovered[0].first_second == doctest::Approx(6.0));
        CHECK(verdict.uncovered[0].last_second == doctest::Approx(7.0));
        CHECK(verdict.uncovered[1].first_second == doctest::Approx(7.5));
        CHECK(verdict.uncovered[1].last_second == doctest::Approx(8.0));
    }
    SUBCASE("a trim interval does not license a copy of altered pixels") {
        const auto verdict = coverage(plan, false, {trim(5.0, 8.0)});
        REQUIRE_FALSE(verdict.ok);
        CHECK(verdict.message.find("lower_third") != std::string::npos);
    }
}

TEST_CASE("an empty declaration keeps the legacy whole-export semantics") {
    auto plan = make_plan();
    plan.layers.push_back(base_video_layer());
    plan.layers.push_back(text_layer("lower_third", chronon3d::Frame{150},
                                     chronon3d::Frame{90}));

    // No intervals: the whole export is treated as changed and nothing is
    // copied, so there is nothing to cover.
    CHECK(cli::validate_copy_lane_coverage(plan, false, {}).ok);
}

TEST_CASE("layers that cannot draw are not additive") {
    auto plan = make_plan();
    plan.layers.push_back(base_video_layer());

    auto invisible = text_layer("hidden", chronon3d::Frame{150}, chronon3d::Frame{90});
    invisible.opacity = 0.0f;
    plan.layers.push_back(invisible);

    auto empty = text_layer("empty", chronon3d::Frame{150}, chronon3d::Frame{90});
    empty.text.clear();
    plan.layers.push_back(empty);

    CHECK(cli::validate_copy_lane_coverage(plan, true, {}).ok);
    CHECK_FALSE(cli::layer_is_additive(plan, 1));
    CHECK_FALSE(cli::layer_is_additive(plan, 2));
}

TEST_CASE("a zero duration means unbounded, not zero-length") {
    // The render path keeps a layer active to the end of the composition when
    // its duration is 0 or absent (direct_yuv_program.cpp). Reading 0 as
    // "draws nothing" would let copy-only publish a base that lacks the layer.
    auto plan = make_plan();
    plan.layers.push_back(base_video_layer());
    plan.layers.push_back(text_layer("banner", chronon3d::Frame{150},
                                     chronon3d::Frame{0}));

    CHECK(cli::layer_is_additive(plan, 1));
    const auto verdict = cli::validate_copy_lane_coverage(plan, true, {});
    REQUIRE_FALSE(verdict.ok);
    REQUIRE(verdict.uncovered.size() == 1);
    CHECK(verdict.uncovered.front().first_second == doctest::Approx(5.0));
    CHECK(verdict.uncovered.front().last_second == doctest::Approx(10.0));
}

TEST_CASE("a layer with no window spans the timeline and must be fully declared") {
    auto plan = make_plan();
    plan.layers.push_back(base_video_layer());
    // No start_frame and no duration_frames: active for the whole timeline.
    plan.layers.push_back(text_layer("watermark", std::nullopt, std::nullopt));

    CHECK_FALSE(coverage(plan, false, {content(5.0, 8.0)}).ok);
    CHECK(coverage(plan, false, {content(0.0, 10.0)}).ok);
}

TEST_CASE("an unprovable timeline or frame rate is refused, never assumed") {
    SUBCASE("no canvas duration") {
        auto plan = make_plan(/*duration_frames=*/0);
        plan.layers.push_back(text_layer("lower_third", chronon3d::Frame{150},
                                         chronon3d::Frame{90}));
        const auto verdict = coverage(plan, false, {content(0.0, 60.0)});
        REQUIRE_FALSE(verdict.ok);
        CHECK(verdict.message.find("lower_third") != std::string::npos);
    }
    SUBCASE("non-positive frame rate") {
        auto plan = make_plan(/*duration_frames=*/300, /*fps_num=*/0, /*fps_den=*/1);
        plan.layers.push_back(text_layer("lower_third", chronon3d::Frame{150},
                                         chronon3d::Frame{90}));
        const auto verdict = coverage(plan, false, {content(0.0, 60.0)});
        REQUIRE_FALSE(verdict.ok);
        CHECK(verdict.message.find("frame rate") != std::string::npos);
    }
    SUBCASE("a layer that starts after the timeline ends draws nothing") {
        auto plan = make_plan();
        plan.layers.push_back(text_layer("late", chronon3d::Frame{400},
                                         chronon3d::Frame{30}));
        CHECK(cli::validate_copy_lane_coverage(plan, true, {}).ok);
    }
}

TEST_CASE("non-integer frame rates convert windows exactly") {
    // 24000/1001 (film) at frame 120 = 5.005 s.
    auto plan = make_plan(/*duration_frames=*/480, /*fps_num=*/24000, /*fps_den=*/1001);
    plan.layers.push_back(text_layer("lower_third", chronon3d::Frame{120},
                                     chronon3d::Frame{120}));

    const auto verdict = cli::validate_copy_lane_coverage(plan, true, {});
    REQUIRE_FALSE(verdict.ok);
    REQUIRE(verdict.uncovered.size() == 1);
    CHECK(verdict.uncovered.front().first_second ==
          doctest::Approx(120.0 * 1001.0 / 24000.0));
    CHECK(verdict.uncovered.front().last_second ==
          doctest::Approx(240.0 * 1001.0 / 24000.0));
}
