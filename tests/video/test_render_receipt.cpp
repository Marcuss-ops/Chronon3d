#include <doctest/doctest.h>

#include <chronon3d/verification/render_receipt.hpp>

#include <filesystem>

TEST_CASE("render receipt reports missing video without dumping probe payload") {
    chronon3d::verification::RenderReceiptInput input;
    input.job_id = "receipt-missing-video";
    input.width = 1920;
    input.height = 1080;
    input.fps_num = 24;
    input.fps_den = 1;
    input.frames = 1;
    input.execution_downgrade_reason = "gop_splice_unsafe";
    input.execution_downgrade_count = 1;

    const std::filesystem::path missing =
        "/tmp/chronon3d_receipt_missing_video.mp4";
    std::error_code error;
    std::filesystem::remove(missing, error);

    const auto receipt = chronon3d::verification::build_render_receipt(
        input, missing, /*is_video=*/true);
    CHECK(receipt.verification.ffprobe == "fail");
    CHECK_FALSE(receipt.copy_eligible);
    CHECK(receipt.media.container.empty());
    CHECK(receipt.media.codec.empty());
    CHECK(receipt.execution_downgrade_reason == "gop_splice_unsafe");
    CHECK(receipt.execution_downgrade_count == 1);
}
