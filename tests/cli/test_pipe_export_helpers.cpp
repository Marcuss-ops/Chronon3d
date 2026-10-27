#include <doctest/doctest.h>

#include <apps/chronon3d_cli/commands/video/common/pipe_export_helpers.hpp>
#include <apps/chronon3d_cli/commands/video/common/pipe_export_queue.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <memory>
#include <stdexcept>

using namespace chronon3d::cli;

TEST_CASE("PipeExportHelpers: segmented output refuses every non-native encoder lane") {
    // The per-item windows are cut by NativeAvEncoder's segmented mux writer.
    // The pipe lane writes ONE container and drops them, which would report a
    // successful batch with zero per-item outputs. Refusal must happen before
    // any encoder or container is opened.
    auto value = chronon3d::composition(
        {.name = "segment-belt",
         .width = 64,
         .height = 64,
         .frame_rate = chronon3d::FrameRate{10, 1},
         .duration = chronon3d::Frame{8}},
        [](const chronon3d::FrameContext&) { return chronon3d::Scene{}; });
    chronon3d::CompiledComposition compiled;
    compiled.composition =
        std::make_shared<const chronon3d::Composition>(std::move(value));
    const chronon3d::CpuBudget budget{};

    FfmpegExportOptions opts;
    opts.output_segments = {
        {"/tmp/segment-belt-0.mp4", 0, 4},
        {"/tmp/segment-belt-1.mp4", 4, 8},
    };
    for (const auto* lane : {"pipe", ""}) {
        opts.encoder.encoder_backend = lane;
        CHECK_FALSE(make_pipe_options(compiled, opts, "libx264", budget)
                        .has_value());
    }
}

TEST_CASE("PipeExportHelpers: progress logs at ten percent intervals and final frame") {
    CHECK(should_log_pipe_progress(1, 10));
    CHECK(should_log_pipe_progress(5, 50));
    CHECK(should_log_pipe_progress(50, 50));
    CHECK_FALSE(should_log_pipe_progress(7, 100));
}

TEST_CASE("PipeExportHelpers: encoded frame count is zero on failure") {
    PipeExportStatus status;
    status.frames_encoded = 42;

    // success defaults to false — frames_encoded returned only on explicit success
    CHECK(pipe_encoded_frame_count(status) == 0);

    status.success = true;
    CHECK(pipe_encoded_frame_count(status) == 42);

    status.success = false;
    CHECK(pipe_encoded_frame_count(status) == 0);
}

TEST_CASE("PipeExportHelpers: failure markers keep exact failure reason") {
    PipeExportStatus cancelled;
    mark_pipe_cancelled(cancelled, 12);
    CHECK_FALSE(cancelled.success);
    CHECK(cancelled.cancelled);
    CHECK_FALSE(cancelled.render_failed);
    CHECK_FALSE(cancelled.writer_error);
    CHECK_FALSE(cancelled.exception_error);

    PipeExportStatus writer;
    mark_pipe_writer_failed(writer, 7);
    CHECK_FALSE(writer.success);
    CHECK(writer.writer_error);
    CHECK_FALSE(writer.cancelled);

    PipeExportStatus render;
    mark_pipe_render_failed(render, 9);
    CHECK_FALSE(render.success);
    CHECK(render.render_failed);
    CHECK_FALSE(render.writer_error);

    PipeExportStatus exception;
    mark_pipe_exception(exception, 3, std::runtime_error("test"));
    CHECK_FALSE(exception.success);
    CHECK(exception.exception_error);
}
