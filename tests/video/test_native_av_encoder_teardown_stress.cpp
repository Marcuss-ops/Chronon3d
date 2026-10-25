#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include "../../apps/chronon3d_cli/utils/video/native_av_encoder.hpp"

#include <chronon3d/media/video/output_contract.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace {

/// Per-frame decode hashes (ffmpeg framemd5 over the raw planes): one line
/// per frame, the last column being the frame hash. The same convention the
/// container-parity tests use, so a wrong timestamp, dropped packet or
/// reordered frame changes the sequence.
std::string decoded_frame_hashes(const std::filesystem::path& mp4) {
    const std::string command =
        "ffmpeg -v error -i '" + mp4.string() + "' -map 0:v -f framemd5 - 2>/dev/null";
    std::string output;
    if (FILE* pipe = popen(command.c_str(), "r")) {
        char buffer[512];
        while (std::fgets(buffer, sizeof(buffer), pipe)) output += buffer;
        pclose(pipe);
    }
    std::string hashes;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto comma = line.rfind(',');
        if (comma == std::string::npos) continue;
        hashes += line.substr(comma + 1);
        hashes += '\n';
    }
    return hashes;
}

} // namespace

TEST_CASE("NativeAvEncoder reset contract is closed before pooling") {
    chronon3d::cli::NativeAvEncoder encoder;
    CHECK(encoder.pool_reset_safe());

    chronon3d::cli::FfmpegPipeOptions options;
    options.width = 0;
    options.height = 0;
    options.fps_num = 0;
    options.fps_den = 0;
    options.output_path = "";
    CHECK_FALSE(encoder.open(options));
    encoder.shutdown_noexcept();
    CHECK(encoder.pool_reset_safe());
}

TEST_CASE("NativeAvEncoder failed open is safe to destroy repeatedly") {
    for (int i = 0; i < 1000; ++i) {
        chronon3d::cli::NativeAvEncoder encoder;
        chronon3d::cli::FfmpegPipeOptions options;
        options.width = 0;
        options.height = 0;
        options.fps_num = 0;
        options.fps_den = 0;
        options.output_path = "";
        CHECK_FALSE(encoder.open(options));
        encoder.shutdown_noexcept();
        encoder.shutdown_noexcept();
    }
}

TEST_CASE("NativeAvEncoder cross-item codec session reuse keeps the MP4-per-item contract") {
    namespace fs = std::filesystem;
    if (std::system("ffmpeg -hide_banner -version >/dev/null 2>&1") != 0) {
        MESSAGE("Skipping — ffmpeg executable is not available");
        return;
    }
    const auto dir = fs::temp_directory_path() / "chronon3d_codec_session_reuse";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    REQUIRE_FALSE(ec);

    // P2 item 2 / P4 item 1 (TICKET-RESIDENT-RENDER-ENGINE /
    // TICKET-BATCH-SUPERJOB): one codec session across consecutive
    // same-configuration items, one MP4 container per item. The env flag is
    // the opt-in gate (default off = historical lifecycle).
    ::setenv("CHRONON_ENCODER_SESSION_REUSE", "1", 1);

    const int frame_counts[] = {24, 24, 7};
    std::vector<std::filesystem::path> outputs;
    bool first = true;
    for (const int count : frame_counts) {
        const auto output =
            dir / ("item-" + std::to_string(outputs.size()) + ".mp4");
        chronon3d::cli::FfmpegPipeOptions options;
        options.width = 320;
        options.height = 240;
        options.fps_num = 24;
        options.fps_den = 1;
        options.codec = "libx264";
        options.output_path = output.string();
        chronon3d::cli::NativeAvEncoder encoder;
        REQUIRE(encoder.open(options));
        // The first item starts a fresh session; every later same-config item
        // must adopt the parked one — this is the cross-item economics the
        // gate measures (no avcodec_open2 per artifact).
        if (first) {
            CHECK_FALSE(encoder.codec_session_adopted());
        } else {
            CHECK(encoder.codec_session_adopted());
        }
        first = false;
        chronon3d::Framebuffer frame(options.width, options.height);
        for (int index = 0; index < count; ++index) {
            REQUIRE(encoder.write_frame(frame));
        }
        CHECK(encoder.frames_written() == static_cast<std::uint64_t>(count));
        REQUIRE(encoder.close());
        outputs.push_back(output);
    }

    // A different configuration must never adopt a foreign session.
    {
        const auto output = dir / "item-other.mp4";
        chronon3d::cli::FfmpegPipeOptions options;
        options.width = 640;
        options.height = 360;
        options.fps_num = 24;
        options.fps_den = 1;
        options.codec = "libx264";
        options.output_path = output.string();
        chronon3d::cli::NativeAvEncoder encoder;
        REQUIRE(encoder.open(options));
        CHECK_FALSE(encoder.codec_session_adopted());
        chronon3d::Framebuffer frame(options.width, options.height);
        for (int index = 0; index < 5; ++index) {
            REQUIRE(encoder.write_frame(frame));
        }
        REQUIRE(encoder.close());
        outputs.push_back(output);
    }

    ::unsetenv("CHRONON_ENCODER_SESSION_REUSE");

    // MP4-per-item contract preserved: every segment is its own artifact with
    // its exact duration and a decodable stream (a segment whose first frame
    // were not IDR — the forced restart keyframe — would not verify).
    const int expected[] = {24, 24, 7, 5};
    const int widths[] = {320, 320, 320, 640};
    const int heights[] = {240, 240, 240, 360};
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        auto contract =
            chronon3d::media::video::resolve_output_contract("youtube_overlay_v1");
        REQUIRE(contract.has_value());
        contract->width = widths[i];
        contract->height = heights[i];
        contract->fps = chronon3d::FrameRate{24, 1};
        contract->frame_count = expected[i];
        contract->audio_required = false;
        contract->audio_streams = 0;
        const auto verification =
            chronon3d::media::video::verify_output_contract(outputs[i], *contract);
        CHECK(verification.passed);
        CHECK(verification.frame_count == expected[i]);
    }

    fs::remove_all(dir, ec);
}

TEST_CASE("P1 static run replicates the anchor packet with exact pixel parity") {
    namespace fs = std::filesystem;
    if (std::system("ffmpeg -hide_banner -version >/dev/null 2>&1") != 0) {
        MESSAGE("Skipping — ffmpeg executable is not available");
        return;
    }
    const auto dir = fs::temp_directory_path() / "chronon3d_p1_static_duplicate";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    REQUIRE_FALSE(ec);

    auto make_options = [&](const char* name) {
        chronon3d::cli::FfmpegPipeOptions options;
        options.width = 320;
        options.height = 240;
        options.fps_num = 24;
        options.fps_den = 1;
        options.codec = "libx264";
        options.output_path = (dir / name).string();
        return options;
    };

    // Reference: the same static content with one real write per frame.
    const auto reference = dir / "reference.mp4";
    {
        auto options = make_options("reference.mp4");
        chronon3d::cli::NativeAvEncoder encoder;
        REQUIRE(encoder.open(options));
        chronon3d::Framebuffer frame(options.width, options.height);
        for (int index = 0; index < 24; ++index) {
            REQUIRE(encoder.write_frame(frame));
        }
        REQUIRE(encoder.close());
    }

    // P1 run (TICKET-STATIC-OVERLAY-DURATION-INVARIANT): two real frames,
    // then the static run is replicated at the packet level from the run
    // anchor — O(1) per frame, no re-render, no re-encode.
    const auto duplicated = dir / "duplicated.mp4";
    {
        auto options = make_options("duplicated.mp4");
        chronon3d::cli::NativeAvEncoder encoder;
        REQUIRE(encoder.open(options));
        chronon3d::Framebuffer frame(options.width, options.height);
        REQUIRE(encoder.write_frame(frame));
        REQUIRE(encoder.write_frame(frame));
        for (int frame_number = 2; frame_number < 24; ++frame_number) {
            REQUIRE(encoder.write_duplicate_frame(frame_number));
        }
        CHECK(encoder.frames_written() == 24u);
        REQUIRE(encoder.close());
    }

    // MP4-per-item contract: exact duration, decodable stream.
    auto contract =
        chronon3d::media::video::resolve_output_contract("youtube_overlay_v1");
    REQUIRE(contract.has_value());
    contract->width = 320;
    contract->height = 240;
    contract->fps = chronon3d::FrameRate{24, 1};
    contract->frame_count = 24;
    contract->audio_required = false;
    contract->audio_streams = 0;
    const auto verification =
        chronon3d::media::video::verify_output_contract(duplicated, *contract);
    CHECK(verification.passed);
    CHECK(verification.frame_count == 24);

    // Pixel parity gate: every duplicated frame decodes to exactly the
    // reference pixels. Static content ⇒ both files decode to one constant
    // frame sequence; nothing differs without a declared re-baseline.
    const auto reference_hashes = decoded_frame_hashes(reference);
    const auto duplicated_hashes = decoded_frame_hashes(duplicated);
    REQUIRE_FALSE(reference_hashes.empty());
    REQUIRE_FALSE(duplicated_hashes.empty());
    CHECK(duplicated_hashes == reference_hashes);

    fs::remove_all(dir, ec);
}

TEST_CASE("NativeAvEncoder partial open cleanup is idempotent") {
    for (int i = 0; i < 1000; ++i) {
        chronon3d::cli::NativeAvEncoder encoder;
        chronon3d::cli::FfmpegPipeOptions options;
        options.width = 1920;
        options.height = 1080;
        options.fps_num = 30;
        options.fps_den = 1;
        options.codec = "libx264";
        options.output_path = "/definitely/missing/chronon3d-stress/output.mp4";
        CHECK_FALSE(encoder.open(options));
        encoder.shutdown_noexcept();
    }
}
