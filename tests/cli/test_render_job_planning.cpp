#include <doctest/doctest.h>

#include "commands.hpp"
#include "utils/job/render_job.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace c3d = chronon3d;
namespace cli = chronon3d::cli;

namespace {

constexpr const char* kCompositionId = "RenderJobBuilderFixture";
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
    };
    return args;
}

} // namespace

TEST_CASE("RenderRequest resolves directly into the canonical RenderJob") {
    auto registry = make_registry();
    auto request = cli::make_render_request(
        registry, args_for("60", "hero.png"));

    REQUIRE(request.has_value());
    CHECK(request->mode == c3d::RenderMode::Still);
    CHECK(request->still_frame == c3d::Frame{60});

    auto resolved = cli::resolve_render_request(
        registry, std::move(*request));
    REQUIRE(resolved.has_value());
    CHECK(resolved->mode == c3d::RenderMode::Still);
    CHECK(resolved->still_frame == c3d::Frame{60});
    CHECK(resolved->output == "hero.png");
    CHECK(resolved->registry == &registry);
    REQUIRE(resolved->comp != nullptr);
    CHECK(resolved->metadata.width == 320);
    CHECK(resolved->metadata.height == 180);
    CHECK(resolved->metadata.duration == c3d::Frame{kDuration});
}

TEST_CASE("RenderJob resolution invokes prepared construction exactly once") {
    auto prepare_calls = std::make_shared<int>(0);
    auto construct_calls = std::make_shared<int>(0);

    c3d::CompositionRegistry registry;
    c3d::CompositionDescriptor descriptor;
    descriptor.id = "PreparedOnce";
    descriptor.prepare_props = [prepare_calls, construct_calls](
        const c3d::CompositionProps&) -> c3d::PreparedCompositionResult {
        ++*prepare_calls;
        c3d::PreparedComposition prepared;
        prepared.metadata = c3d::CompositionMetadata{
            .width = 320,
            .height = 180,
            .fps = c3d::FrameRate{30, 1},
            .duration = c3d::Frame{20},
        };
        prepared.construct = [construct_calls] {
            ++*construct_calls;
            return make_fixture_composition();
        };
        return prepared;
    };
    registry.add(std::move(descriptor));

    auto args = args_for("0", "prepared.png");
    args.comp_id = "PreparedOnce";
    auto job = cli::make_render_job(registry, args);

    REQUIRE(job.has_value());
    CHECK(*prepare_calls == 1);
    CHECK(*construct_calls == 1);
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

TEST_CASE("RenderJob planner preserves canonical video settings") {
    auto registry = make_registry();
    auto args = args_for("0-30", "hero.mp4");
    args.video_settings.fps = 60;
    args.video_settings.crf = 20;
    args.video_settings.codec = "custom_codec";
    args.video_settings.encode_preset = "medium";
    args.video_settings.tune = "film";
    args.video_settings.keep_frames = true;
    args.video_settings.frames_dir = "custom_frames";
    args.video_settings.chunks = 4;
    args.video_settings.hardware_encoder = "none";
    args.video_settings.ffmpeg_mode = "png";
    args.video_settings.ffmpeg_verbose = true;
    args.video_settings.pipe_pixfmt = "nv12";
    args.video_settings.color_output = "rec709";
    args.video_settings.pipe_writer = "classic";
    args.video_settings.encoder_backend = "pipe";
    args.video_settings.dry_run = true;

    auto job = cli::make_render_job(registry, args);

    REQUIRE(job.has_value());
    REQUIRE(job->mode == c3d::RenderMode::Video);
    CHECK(job->video_settings.fps == 60);
    CHECK(job->video_settings.crf == 20);
    CHECK(job->video_settings.codec == "custom_codec");
    CHECK(job->video_settings.encode_preset == "medium");
    CHECK(job->video_settings.tune == "film");
    CHECK(job->video_settings.keep_frames);
    CHECK(job->video_settings.frames_dir == "custom_frames");
    CHECK(job->video_settings.chunks == 4);
    CHECK(job->video_settings.ffmpeg_mode == "png");
    CHECK(job->video_settings.ffmpeg_verbose);
    CHECK(job->video_settings.pipe_pixfmt == "nv12");
    CHECK(job->video_settings.color_output == "rec709");
    CHECK(job->video_settings.pipe_writer == "classic");
    CHECK(job->video_settings.encoder_backend == "pipe");
    CHECK(job->video_settings.dry_run);
    CHECK(job->execution.warmup_renderer);
    CHECK(job->execution.warmup_dummy_frame);
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
