#include <doctest/doctest.h>

#include <chronon3d/verification/render_receipt.hpp>

#include <filesystem>
#include <fstream>
#include <string>

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

TEST_CASE("render receipt consumes precomputed output facts (fix 2 dedup)") {
    // A file whose bytes are NOT a decodable video: the receipt's own probe
    // would fail it. With caller-precomputed facts the builder must consume
    // them verbatim and never probe or re-hash the artifact.
    const std::filesystem::path output =
        "/tmp/chronon3d_receipt_precomputed.mp4";
    {
        std::ofstream payload(output);
        payload << "not a real mp4";
    }

    chronon3d::verification::RenderReceiptInput input;
    input.job_id = "receipt-precomputed";
    input.width = 1920;
    input.height = 1080;
    input.fps_num = 24;
    input.fps_den = 1;
    input.frames = 24;

    chronon3d::verification::MediaContract media;
    media.container = "mov,mp4,m4a,3gp,3g2,mj2";
    media.codec = "h264";
    media.pixel_format = "yuv420p";
    media.width = 1920;
    media.height = 1080;
    media.fps_num = 24;
    media.fps_den = 1;
    media.r_fps_num = 24;
    media.r_fps_den = 1;
    media.frame_count = 24;
    media.has_video = true;
    media.video_streams = 1;
    media.probed = true;

    input.precomputed_media = media;
    input.precomputed_sha256 = std::string(64, 'a');
    input.precomputed_probe_ms = 1.5;
    input.precomputed_sha256_ms = 2.5;

    const auto receipt = chronon3d::verification::build_render_receipt(
        input, output, /*is_video=*/true);

    CHECK(receipt.verification.ffprobe == "pass");
    CHECK(receipt.sha256 == std::string(64, 'a'));
    CHECK(receipt.media.codec == "h264");
    CHECK(receipt.media.container == "mov,mp4,m4a,3gp,3g2,mj2");
    CHECK(receipt.timing.probe_ms == 1.5);
    CHECK(receipt.timing.sha256_ms == 2.5);
    CHECK(receipt.verification.codec == "pass");
    CHECK(receipt.verification.resolution == "pass");
    CHECK(receipt.verification.fps == "pass");
    CHECK(receipt.verification.frame_count == "pass");
    CHECK(receipt.copy_eligible);

    std::error_code error;
    std::filesystem::remove(output, error);
}

TEST_CASE("render receipt runs its own probe when HDR mastering must match") {
    // Precomputed facts cannot carry the mastering-display side data the HDR
    // comparison consumes, so the builder must fall back to its own probe —
    // and on a non-video artifact that probe fails loudly instead of
    // trusting the precomputed contract.
    const std::filesystem::path output =
        "/tmp/chronon3d_receipt_hdr_fallback.mp4";
    {
        std::ofstream payload(output);
        payload << "not a real mp4";
    }

    chronon3d::verification::RenderReceiptInput input;
    input.job_id = "receipt-hdr-fallback";
    input.width = 1920;
    input.height = 1080;
    input.fps_num = 24;
    input.fps_den = 1;
    input.frames = 24;
    input.hdr_mastering_metadata = chronon3d::color::HdrMasteringMetadata{};

    chronon3d::verification::MediaContract media;
    media.container = "mov,mp4";
    media.codec = "h264";
    media.width = 1920;
    media.height = 1080;
    media.probed = true;
    input.precomputed_media = media;
    input.precomputed_sha256 = std::string(64, 'b');

    const auto receipt = chronon3d::verification::build_render_receipt(
        input, output, /*is_video=*/true);

    CHECK(receipt.verification.ffprobe == "fail");
    CHECK(receipt.media.container.empty());
    CHECK_FALSE(receipt.copy_eligible);

    std::error_code error;
    std::filesystem::remove(output, error);
}
