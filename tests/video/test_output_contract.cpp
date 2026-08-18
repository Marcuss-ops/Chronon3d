// ═══════════════════════════════════════════════════════════════════════════
// tests/video/test_output_contract.cpp
//
// Locks the canonical OutputContract resolution + verification contract:
//   - youtube_overlay_v1 resolves to 1920×1080 / 30fps / h264 / yuv420p
//     with audio policy, and unknown profiles fail loud.
//   - assets::sha256_file hashes a file's bytes with the canonical SHA-256
//     primitive (identical to sha256_string of the same bytes).
//   - verify_output_contract fails structural checks on missing files, and
//     separates `passed` (decodable + geometry) from `copy_eligible`
//     (media contract + SHA-256).
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/media/video/output_contract.hpp>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using chronon3d::media::video::OutputContract;
using chronon3d::media::video::resolve_output_contract;
using chronon3d::media::video::verify_output_contract;

std::string temp_path(const char* suffix) {
    static std::atomic<unsigned> counter{0};
    return "/tmp/chronon3d_output_contract_" +
           std::to_string(counter.fetch_add(1)) + "_" + suffix;
}

bool ffmpeg_available() {
    return std::system("ffmpeg -version >/dev/null 2>&1") == 0 &&
           std::system("ffprobe -version >/dev/null 2>&1") == 0;
}

}  // namespace

TEST_CASE("resolve_output_contract: youtube_overlay_v1 resolves canonically") {
    auto result = resolve_output_contract("youtube_overlay_v1");
    REQUIRE(result);
    const auto& c = result.value();
    CHECK(c.width == 1920);
    CHECK(c.height == 1080);
    CHECK(c.fps.num() == 30);
    CHECK(c.fps.den() == 1);
    CHECK(c.video_codec == "h264");
    CHECK(c.pixel_format == "yuv420p");
    CHECK(c.audio_required);
    CHECK(c.audio_streams == 1);
}

TEST_CASE("resolve_output_contract: unknown profile fails loud") {
    auto result = resolve_output_contract("does_not_exist");
    REQUIRE_FALSE(result);
    CHECK(result.error().find("does_not_exist") != std::string::npos);
}

TEST_CASE("assets::sha256_file matches sha256_string of the same bytes") {
    const std::string payload = "chronon-output-contract-sha256-fixture";
    const auto path = temp_path("hash.bin");
    {
        std::ofstream out(path, std::ios::binary);
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }

    const auto file_digest = chronon3d::assets::sha256_file(path);
    REQUIRE(file_digest.has_value());
    const auto string_digest = chronon3d::assets::sha256_string(payload);
    CHECK(file_digest->hex() == string_digest.hex());
    CHECK(file_digest->hex().size() == 64);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_CASE("assets::sha256_file returns nullopt for a missing file") {
    CHECK_FALSE(chronon3d::assets::sha256_file(
        "/tmp/chronon3d_output_contract_definitely_missing.bin").has_value());
}

TEST_CASE("verify_output_contract fails structural checks on a missing file") {
    OutputContract contract;
    contract.width = 1920;
    contract.height = 1080;
    contract.audio_required = false;
    contract.audio_streams = 0;

    const auto result = verify_output_contract(
        "/tmp/chronon3d_output_contract_missing.mp4", contract);
    REQUIRE_FALSE(result.passed);
    CHECK_FALSE(result.copy_eligible);
    CHECK(result.sha256.empty());  // no digest computed
}

TEST_CASE("verify_output_contract: full pass sets copy_eligible") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg/ffprobe required");
        return;
    }
    const auto artifact = temp_path("verified.mp4");
    const std::string make =
        "ffmpeg -hide_banner -loglevel error -y "
        "-f lavfi -i color=c=black:s=1920x1080:r=30 "
        "-frames:v 3 -c:v libx264 -pix_fmt yuv420p " + artifact;
    REQUIRE(std::system(make.c_str()) == 0);

    OutputContract contract;
    contract.width = 1920;
    contract.height = 1080;
    contract.fps = chronon3d::FrameRate{30, 1};
    contract.video_codec = "h264";
    contract.pixel_format = "yuv420p";
    contract.audio_required = false;
    contract.audio_streams = 0;
    contract.frame_count = 3;

    const auto result = verify_output_contract(artifact, contract);
    CHECK(result.passed);
    CHECK(result.copy_eligible);
    CHECK(result.width == 1920);
    CHECK(result.height == 1080);
    CHECK(result.video_codec == "h264");
    CHECK(result.pixel_format == "yuv420p");
    CHECK(result.sha256.size() == 64);

    std::error_code ec;
    std::filesystem::remove(artifact, ec);
    std::filesystem::remove(artifact + ".chronon.probe.json", ec);
}

TEST_CASE("verify_output_contract: pix_fmt mismatch passes but not copy_eligible") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg/ffprobe required");
        return;
    }
    const auto artifact = temp_path("wrong_pixfmt.mp4");
    const std::string make =
        "ffmpeg -hide_banner -loglevel error -y "
        "-f lavfi -i color=c=black:s=1920x1080:r=30 "
        "-frames:v 1 -c:v libx264 -pix_fmt yuv420p " + artifact;
    REQUIRE(std::system(make.c_str()) == 0);

    // Expect yuv444p — the artifact is decodable (passed) but violates the
    // media contract, so copy_eligible must stay false.
    OutputContract contract;
    contract.width = 1920;
    contract.height = 1080;
    contract.fps = chronon3d::FrameRate{30, 1};
    contract.video_codec = "h264";
    contract.pixel_format = "yuv444p";
    contract.audio_required = false;
    contract.audio_streams = 0;

    const auto result = verify_output_contract(artifact, contract);
    CHECK(result.passed);
    CHECK_FALSE(result.copy_eligible);
    CHECK_FALSE(result.sha256.empty());

    std::error_code ec;
    std::filesystem::remove(artifact, ec);
    std::filesystem::remove(artifact + ".chronon.probe.json", ec);
}
