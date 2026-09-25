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

TEST_CASE("render receipt contract: preserves software and direct_yuv lane") {
    SUBCASE("job software -> render_lane == software") {
        chronon3d::verification::RenderReceiptInput input;
        input.job_id = "receipt-software-contract";
        input.backend = "software";
        input.render_lane = "software";
        input.lane_reason = "selected";

        const auto receipt = chronon3d::verification::build_render_receipt(
            input, "/tmp/chronon3d_receipt_contract_test.mp4", /*is_video=*/false);
        CHECK(receipt.render_lane == "software");
        CHECK(receipt.lane_reason == "selected");
    }

    SUBCASE("job direct_yuv -> render_lane == direct_yuv_*") {
        chronon3d::verification::RenderReceiptInput input;
        input.job_id = "receipt-direct-yuv-contract";
        input.backend = "gpu";
        input.render_lane = "direct_yuv_cuda";
        input.lane_reason = "selected";

        const auto receipt = chronon3d::verification::build_render_receipt(
            input, "/tmp/chronon3d_receipt_contract_test.mp4", /*is_video=*/false);
        CHECK(receipt.render_lane == "direct_yuv_cuda");
        CHECK(receipt.lane_reason == "selected");
    }
}
