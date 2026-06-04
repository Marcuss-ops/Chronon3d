#include <doctest/doctest.h>

#include <apps/chronon3d_cli/commands/video/pipe_export_helpers.hpp>

#include <stdexcept>

using namespace chronon3d;
using namespace chronon3d::cli;

TEST_CASE("PipeExportHelpers: progress logs at ten percent intervals and final frame") {
    CHECK(should_log_pipe_progress(1, 10));
    CHECK(should_log_pipe_progress(5, 50));
    CHECK(should_log_pipe_progress(50, 50));
    CHECK_FALSE(should_log_pipe_progress(7, 100));
}

TEST_CASE("PipeExportHelpers: encoded frame count is zero on failure") {
    PipeExportStatus status;
    status.frames_written = 42;

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
