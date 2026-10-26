#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include "../../apps/chronon3d_cli/utils/video/native_av_encoder.hpp"

#include <chronon3d/media/video/output_contract.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <optional>
#include <vector>

namespace {

/// Per-frame decode hashes (ffmpeg framemd5 over the raw planes): one line
/// per frame, the last column being the frame hash. The same convention the
/// container-parity tests use, so a wrong timestamp, dropped packet or
/// reordered frame changes the sequence.
class ScopedEnvironmentValue {
public:
    ScopedEnvironmentValue(std::string name, std::string value)
        : m_name(std::move(name)) {
        if (const char* previous = std::getenv(m_name.c_str())) {
            m_previous = previous;
        }
        set(std::move(value));
    }

    ~ScopedEnvironmentValue() {
        if (m_previous) {
            ::setenv(m_name.c_str(), m_previous->c_str(), 1);
        } else {
            ::unsetenv(m_name.c_str());
        }
    }

    void set(std::optional<std::string> value) {
        if (value) {
            ::setenv(m_name.c_str(), value->c_str(), 1);
        } else {
            ::unsetenv(m_name.c_str());
        }
    }

private:
    std::string m_name;
    std::optional<std::string> m_previous;
};

/// Deterministic content helper: the painted phase advances every frame
/// until `hold_start` and freezes there, modeling a card whose reveal settles
/// into a hold (the golden overlay mix's dominant shape).
void paint_frame(chronon3d::Framebuffer& frame, int frame_index, int hold_start) {
    const int frozen = std::min(frame_index, hold_start);
    const float phase = static_cast<float>(frozen) * 0.13f;
    for (int y = 0; y < frame.height(); ++y) {
        chronon3d::Color* row = frame.pixels_row(y);
        for (int x = 0; x < frame.width(); ++x) {
            row[x] = chronon3d::Color{
                static_cast<float>(x) / static_cast<float>(frame.width()),
                static_cast<float>(y) / static_cast<float>(frame.height()),
                phase, 1.0f};
        }
    }
}

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

struct DuplicateParityResult {
    std::string reference_hashes;
    std::string duplicated_hashes;
    bool replicated{false};
};

/// Reference vs duplicated encode of the same frame sequence. The duplicated
/// variant renders `real_frames` frames and emits the rest as duplicates of the
/// last rendered frame; `hold_start` must have been reached by the last real
/// frame so the duplicated frames' content is that frame's content.
DuplicateParityResult duplicate_parity_pair(
    const std::filesystem::path& dir, const char* codec,
    const std::shared_ptr<chronon3d::media::VideoDeviceRuntime>& device_runtime,
    int total_frames, int hold_start, int real_frames,
    const char* tune = nullptr) {
    namespace fs = std::filesystem;
    const auto reference = dir / (std::string("reference_") + codec + ".mp4");
    const auto duplicated = dir / (std::string("duplicated_") + codec + ".mp4");
    auto make_options = [&](const fs::path& name) {
        chronon3d::cli::FfmpegPipeOptions options;
        options.width = 320;
        options.height = 240;
        options.fps_num = 24;
        options.fps_den = 1;
        options.codec = codec;
        options.output_path = name.string();
        if (tune) {
            options.tune = tune;
            options.tune_explicit = true;
        }
        // Single encode thread: frame threading would delay the content
        // root's packet past the run-mode decision, and these tests are about
        // the duplicate contract, not encoder parallelism.
        options.encode_threads = 1;
        return options;
    };
    {
        auto options = make_options(reference);
        chronon3d::cli::NativeAvEncoder encoder(device_runtime);
        REQUIRE(encoder.open(options));
        chronon3d::Framebuffer frame(options.width, options.height);
        for (int index = 0; index < total_frames; ++index) {
            paint_frame(frame, index, hold_start);
            REQUIRE(encoder.write_frame(frame));
        }
        REQUIRE(encoder.close());
    }
    DuplicateParityResult result;
    {
        auto options = make_options(duplicated);
        chronon3d::cli::NativeAvEncoder encoder(device_runtime);
        REQUIRE(encoder.open(options));
        chronon3d::Framebuffer frame(options.width, options.height);
        for (int index = 0; index < real_frames; ++index) {
            paint_frame(frame, index, hold_start);
            REQUIRE(encoder.write_frame(frame));
        }
        for (int index = real_frames; index < total_frames; ++index) {
            REQUIRE(encoder.write_duplicate_frame(index));
        }
        result.replicated = encoder.duplicate_run_replicated();
        CHECK(encoder.frames_written() ==
              static_cast<std::uint64_t>(total_frames));
        REQUIRE(encoder.close());
    }
    result.reference_hashes = decoded_frame_hashes(reference);
    result.duplicated_hashes = decoded_frame_hashes(duplicated);
    return result;
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

TEST_CASE("NativeAvEncoder fails closed on reuse after MP4 finalization") {
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
    // TICKET-BATCH-SUPERJOB): each MP4 is finalized before its next item.
    // That sends EOF to FFmpeg, so even the experimental env flag must not
    // cause an already-drained codec to be adopted for another artifact.
    ScopedEnvironmentValue reuse_scope("CHRONON_ENCODER_SESSION_REUSE", "1");

    const int frame_counts[] = {24, 24, 7};
    std::vector<std::filesystem::path> outputs;
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
        // Reuse after MP4 EOF is unsupported; every artifact must start a
        // fresh codec until the batched muxer can own one unflushed session.
        CHECK_FALSE(encoder.codec_session_adopted());
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

    // Reference item: same frame sequence/configuration without cross-item
    // reuse. Decoded frame hashes must match the adopted-session artifact,
    // not merely its frame count and MP4 metadata.
    const auto reference = dir / "item-reference.mp4";
    reuse_scope.set(std::nullopt);
    {
        chronon3d::cli::FfmpegPipeOptions options;
        options.width = 320;
        options.height = 240;
        options.fps_num = 24;
        options.fps_den = 1;
        options.codec = "libx264";
        options.output_path = reference.string();
        chronon3d::cli::NativeAvEncoder encoder;
        REQUIRE(encoder.open(options));
        CHECK_FALSE(encoder.codec_session_adopted());
        chronon3d::Framebuffer frame(options.width, options.height);
        for (int index = 0; index < 24; ++index) {
            REQUIRE(encoder.write_frame(frame));
        }
        REQUIRE(encoder.close());
    }
    reuse_scope.set(std::string{"1"});

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

    auto reference_contract =
        chronon3d::media::video::resolve_output_contract("youtube_overlay_v1");
    REQUIRE(reference_contract.has_value());
    reference_contract->width = 320;
    reference_contract->height = 240;
    reference_contract->fps = chronon3d::FrameRate{24, 1};
    reference_contract->frame_count = 24;
    reference_contract->audio_required = false;
    reference_contract->audio_streams = 0;
    const auto reference_verification =
        chronon3d::media::video::verify_output_contract(reference, *reference_contract);
    CHECK(reference_verification.passed);
    CHECK(reference_verification.frame_count == 24);
    const auto reference_hashes = decoded_frame_hashes(reference);
    REQUIRE_FALSE(reference_hashes.empty());
    CHECK(decoded_frame_hashes(outputs[1]) == reference_hashes);

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

TEST_CASE("P1 replicate-mode run re-emits the anchor packet with exact pixel parity") {
    namespace fs = std::filesystem;
    if (std::system("ffmpeg -hide_banner -version >/dev/null 2>&1") != 0) {
        MESSAGE("Skipping — ffmpeg executable is not available");
        return;
    }
    const auto dir = fs::temp_directory_path() / "chronon3d_p1_replicate_mode";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    REQUIRE_FALSE(ec);

    // Static card from frame 0: the content-stretch root is the session's
    // first frame, whose packet is a key packet, so the run must take the
    // packet-replication path (the O(1) P1 contract). The zerolatency tune
    // makes the codec publish packets immediately, so the mode decision sees
    // the root packet at run start (as the production CLI config does).
    const auto result = duplicate_parity_pair(
        dir, "libx264", nullptr, /*total_frames=*/24,
        /*hold_start=*/0, /*real_frames=*/1, /*tune=*/"zerolatency");
    REQUIRE_FALSE(result.reference_hashes.empty());
    CHECK(result.replicated);

    // Exact pixel contract of the replicate path: every replica decodes to
    // the content-root frame's OWN pixels — the run shows that frame, never a
    // stale one, with zero drift across the run. (A full-render stream of the
    // same content may reconstruct with per-frame quantization noise; the
    // replica stream is exactly the root frame on every tick.)
    auto hash_lines = [](const std::string& blob) {
        std::vector<std::string> out;
        std::istringstream stream(blob);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty()) out.push_back(line);
        }
        return out;
    };
    const auto duplicated = hash_lines(result.duplicated_hashes);
    const auto reference = hash_lines(result.reference_hashes);
    REQUIRE(duplicated.size() == 24);
    REQUIRE_FALSE(reference.empty());
    for (const auto& hash : duplicated) {
        CHECK(hash == duplicated.front());
    }
    CHECK(duplicated.front() == reference.front());

    fs::remove_all(dir, ec);
}

TEST_CASE("P1 duplicate runs keep exact pixel parity (resubmit path)") {
    namespace fs = std::filesystem;
    if (std::system("ffmpeg -hide_banner -version >/dev/null 2>&1") != 0) {
        MESSAGE("Skipping — ffmpeg executable is not available");
        return;
    }
    const auto dir = fs::temp_directory_path() / "chronon3d_p1_resubmit_parity";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    REQUIRE_FALSE(ec);

    const auto run_codec = [&](const char* codec,
                               const std::shared_ptr<chronon3d::media::VideoDeviceRuntime>& rt) {
        {
            // Static card with two real frames: the content root's packet is
            // a P packet (not replicable), so the run must resubmit — and the
            // encoder then sees exactly the frame sequence the reference
            // encode sees, so the two streams are bit-exact by construction.
            const auto static_case = duplicate_parity_pair(
                dir, codec, rt, /*total_frames=*/24,
                /*hold_start=*/0, /*real_frames=*/2);
            CHECK_FALSE(static_case.replicated);
            REQUIRE_FALSE(static_case.reference_hashes.empty());
            CHECK(static_case.duplicated_hashes == static_case.reference_hashes);
        }
        {
            // Reveal-then-hold card (the golden mix's dominant shape): eight
            // changing frames, the hold's first frame rendered, the rest
            // duplicates. The hold must show the settled frame's pixels —
            // exact parity with the full-render stream, stale-pixel free.
            const auto hold_case = duplicate_parity_pair(
                dir, codec, rt, /*total_frames=*/24,
                /*hold_start=*/8, /*real_frames=*/9);
            CHECK_FALSE(hold_case.replicated);
            REQUIRE_FALSE(hold_case.reference_hashes.empty());
            CHECK(hold_case.duplicated_hashes == hold_case.reference_hashes);
        }
    };

    run_codec("libx264", nullptr);

    // NVENC: the same parity must hold through the resubmit path (hardware
    // sessions never forge packets). Skips cleanly when no NVENC is usable.
    auto runtimes = std::make_shared<chronon3d::media::VideoRuntimeRegistry>();
    auto device_runtime = runtimes->get_or_create(0, nullptr, -1);
    if (device_runtime) {
        chronon3d::cli::FfmpegPipeOptions probe;
        probe.width = 320;
        probe.height = 240;
        probe.fps_num = 24;
        probe.fps_den = 1;
        probe.codec = "h264_nvenc";
        probe.output_path = (dir / "nvenc_probe.mp4").string();
        chronon3d::cli::NativeAvEncoder probe_encoder(device_runtime);
        const bool nvenc_ok = probe_encoder.open(probe);
        probe_encoder.shutdown_noexcept();
        if (nvenc_ok) {
            run_codec("h264_nvenc", device_runtime);
        } else {
            MESSAGE("Skipping h264_nvenc — encoder open failed on this host");
        }
    } else {
        MESSAGE("Skipping h264_nvenc — no video device runtime");
    }

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
