#include <doctest/doctest.h>

#include <apps/chronon3d_cli/commands/video/common/pipe_export_helpers.hpp>
#include <apps/chronon3d_cli/commands/video/common/pipe_export_queue.hpp>

#include <stdexcept>
#include <chrono>
#include <thread>

using namespace chronon3d::cli;

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

TEST_CASE("FrameInteropRing: bounded triple buffer records real contention") {
    FrameInteropRing ring;
    const auto first = ring.acquire();
    const auto second = ring.acquire();
    const auto third = ring.acquire();
    REQUIRE(first != FrameInteropRing::kInvalidSlot);
    REQUIRE(second != FrameInteropRing::kInvalidSlot);
    REQUIRE(third != FrameInteropRing::kInvalidSlot);

    std::size_t acquired = FrameInteropRing::kInvalidSlot;
    std::thread waiter([&] { acquired = ring.acquire(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ring.release(second);
    waiter.join();

    CHECK(acquired == second);
    CHECK(ring.wait_count() == 1);
    CHECK(ring.wait_us() > 0);
    ring.release(first);
    ring.release(third);
    ring.release(acquired);
}

TEST_CASE("FrameInteropRing: close cancels future acquisition") {
    FrameInteropRing ring;
    ring.close();
    CHECK(ring.acquire() == FrameInteropRing::kInvalidSlot);
}
