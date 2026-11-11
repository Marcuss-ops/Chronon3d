#include <doctest/doctest.h>

#include <apps/chronon3d_cli/commands/video/common/pipe_export_helpers.hpp>
#include <apps/chronon3d_cli/commands/video/common/pipe_export_queue.hpp>
#include <chronon3d/media/video/video_job_execution_context.hpp>

#include <memory>
#include <stdexcept>

using namespace chronon3d::cli;

TEST_CASE("PipeExportHelpers: segmented output refuses every non-native encoder lane") {
    // The per-item windows are cut by NativeAvEncoder's segmented mux writer.
    // The pipe lane writes ONE container and drops them, which would report a
    // successful batch with zero per-item outputs. Refusal must happen before
    // any encoder or container is opened.
    const chronon3d::CpuBudget budget{};

    FfmpegExportOptions opts;
    opts.output_segments = {
        {"/tmp/segment-belt-0.mp4", 0, 4},
        {"/tmp/segment-belt-1.mp4", 4, 8},
    };
    for (const auto* lane : {"pipe", ""}) {
        opts.encoder.encoder_backend = lane;
        CHECK_FALSE(make_pipe_options(64, 64, opts, "libx264", budget)
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
    CHECK(writer.failure_stage == "writer");
    CHECK_FALSE(writer.failure_message.empty());

    PipeExportStatus render;
    mark_pipe_render_failed(render, 9);
    CHECK_FALSE(render.success);
    CHECK(render.render_failed);
    CHECK_FALSE(render.writer_error);
    CHECK(render.failure_stage == "render");
    CHECK_FALSE(render.failure_message.empty());

    PipeExportStatus exception;
    mark_pipe_exception(exception, 3, std::runtime_error("test"));
    CHECK_FALSE(exception.success);
    CHECK(exception.exception_error);
    CHECK(exception.failure_stage == "exception");
    CHECK(exception.failure_message == "test");
}

TEST_CASE("VideoExportFailure: device memory and capability failures are typed") {
    using chronon3d::media::VideoExportFailureClass;
    using chronon3d::media::classify_export_failure;

    CHECK(classify_export_failure("") == VideoExportFailureClass::None);
    CHECK(classify_export_failure(
              "VmaCreateImage failed with VkResult=-2") ==
          VideoExportFailureClass::DeviceMemory);
    CHECK(classify_export_failure(
              "VK_ERROR_OUT_OF_HOST_MEMORY while uploading") ==
          VideoExportFailureClass::DeviceMemory);
    CHECK(classify_export_failure(
              "UnsupportedCapability: nvenc is unavailable") ==
          VideoExportFailureClass::UnsupportedCapability);
    CHECK(classify_export_failure("RenderFailed: graph node exploded") ==
          VideoExportFailureClass::Render);
}
