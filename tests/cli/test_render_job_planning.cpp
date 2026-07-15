#include <doctest/doctest.h>

#include "commands.hpp"
#include "utils/job/render_job.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>

#include <memory>
#include <string>

namespace c3d = chronon3d;
namespace cli = chronon3d::cli;

namespace {

constexpr const char* kCompositionId = "RenderJobPlanningFixture";
constexpr std::int64_t kDuration = 120;

c3d::Composition make_fixture_composition() {
    return c3d::Composition{
        c3d::CompositionSpec{
            .name = kCompositionId,
            .width = 320,
            .height = 180,
            .frame_rate = c3d::FrameRate{30, 1},
            .duration = c3d::Frame{kDuration},
            .assets_root = {},
        },
        [](const c3d::FrameContext&) { return c3d::Scene{}; },
    };
}

c3d::CompositionRegistry make_registry() {
    c3d::CompositionRegistry registry;
    registry.add(c3d::CompositionDescriptor{
        .id = kCompositionId,
        .width = 320,
        .height = 180,
        .fps = c3d::FrameRate{30, 1},
        .duration = c3d::Frame{kDuration},
        .factory = [](const c3d::CompositionProps&) {
            return make_fixture_composition();
        },
    });
    return registry;
}

cli::RenderArgs args_for(std::string frames, std::string output) {
    cli::RenderArgs args;
    args.comp_id = kCompositionId;
    args.frames = std::move(frames);
    args.output = std::move(output);
    args.cpu_budget = c3d::CpuBudget{
        .total_threads = 1,
        .render_threads = 1,
        .decode_threads = 0,
        .encode_threads = 0,
        .writer_threads = 0,
    };
    return args;
}

} // namespace

TEST_CASE("RenderJob planner selects Still for one image frame") {
    auto registry = make_registry();
    auto job = cli::make_render_job(registry, args_for("60", "hero.png"));

    REQUIRE(job.has_value());
    CHECK(job->mode == c3d::RenderMode::Still);
    CHECK(job->still_frame == c3d::Frame{60});
    CHECK(job->output == "hero.png");
    CHECK(job->registry == &registry);
    REQUIRE(job->comp != nullptr);
}

TEST_CASE("RenderJob planner selects Sequence and preserves frame step") {
    auto registry = make_registry();
    auto job = cli::make_render_job(
        registry, args_for("0-90x5", "frame_####.png"));

    REQUIRE(job.has_value());
    CHECK(job->mode == c3d::RenderMode::Sequence);
    CHECK(job->first_frame == c3d::Frame{0});
    CHECK(job->last_frame == c3d::Frame{90});
    CHECK(job->frame_step == c3d::Frame{5});
}

TEST_CASE("RenderJob planner selects Video from a case-insensitive extension") {
    auto registry = make_registry();

    SUBCASE("default frame input expands to the full composition") {
        auto job = cli::make_render_job(registry, args_for("0", "hero.MP4"));
        REQUIRE(job.has_value());
        CHECK(job->mode == c3d::RenderMode::Video);
        CHECK(job->first_frame == c3d::Frame{0});
        CHECK(job->last_frame == c3d::Frame{kDuration - 1});
    }

    SUBCASE("an explicit range remains bounded") {
        auto job = cli::make_render_job(registry, args_for("10-30", "hero.webm"));
        REQUIRE(job.has_value());
        CHECK(job->mode == c3d::RenderMode::Video);
        CHECK(job->first_frame == c3d::Frame{10});
        CHECK(job->last_frame == c3d::Frame{30});
    }

    SUBCASE("an explicit non-zero frame creates a one-frame video") {
        auto job = cli::make_render_job(registry, args_for("12", "hero.mov"));
        REQUIRE(job.has_value());
        CHECK(job->mode == c3d::RenderMode::Video);
        CHECK(job->first_frame == c3d::Frame{12});
        CHECK(job->last_frame == c3d::Frame{12});
    }
}

TEST_CASE("RenderJob planner keeps non-video uppercase extensions on image modes") {
    auto registry = make_registry();
    auto job = cli::make_render_job(registry, args_for("8", "hero.PNG"));

    REQUIRE(job.has_value());
    CHECK(job->mode == c3d::RenderMode::Still);
    CHECK(job->still_frame == c3d::Frame{8});
}

TEST_CASE("RenderJob planner fails cleanly for an unknown composition") {
    auto registry = make_registry();
    auto args = args_for("0", "missing.png");
    args.comp_id = "MissingComposition";

    CHECK_FALSE(cli::make_render_job(registry, args).has_value());
}

#ifdef CHRONON3D_TEST_VIDEO_EXPORT_DISABLED
TEST_CASE("Video RenderJob returns UnsupportedMode when exporter target is disabled") {
    auto registry = make_registry();
    auto job = cli::make_render_job(registry, args_for("0", "hero.mp4"));
    REQUIRE(job.has_value());

    const auto result = cli::execute_render_job(*job);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == c3d::RenderJobErrorCode::UnsupportedMode);
}
#endif
