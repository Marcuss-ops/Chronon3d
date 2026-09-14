// Deterministic regression coverage for the retained-frame decode window
// (commit 6d6d9d5 "Fix native video rendering pipeline").
//
// The fix replays duplicate or slightly-out-of-order sample requests — the
// two-layers-share-one-source pattern, FPS-mapping repeats — from a small
// window of recently materialized frames instead of seeking back to the
// previous keyframe and re-decoding the GOP (~100-200ms per repeated index).
//
// decode_native_frame_at() hands whatever the decoder materialized (CUDA
// surface OR software frame) back to the caller as an owned AVFrame ref, so
// the replay path is exercised deterministically without a GPU: no CUDA/NVDEC
// device is required.
//
// Regression assertion: the retained-replay counter (decode_profiling_stats)
// increments on a repeated/regressed sample index and stays flat when a NEW
// sample is decoded — the pre-fix decoder never incremented it because every
// repeated request went through a fresh keyframe seek + decode.
//
// SCOPE NOTE: this test drives the native-capture materialization path only
// (the production GPU hot path). The software sws materialization path has a
// separate pre-existing defect — the second frame_to_framebuffer conversion
// in one session corrupts the heap on the pinned FFmpeg 4.x — tracked in
// TICKET-NATIVE-SOFTWARE-SWS-MULTIFRAME-CORRUPTION; keep conversions here on
// the capture path which refs decoded frames and never runs sws.

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>
#include <chronon3d/runtime/gpu_runtime.hpp>
#include <spdlog/spdlog.h>
#include <cuda.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#ifdef _WIN32
#  include <process.h>
#  define CHRONON3D_GETPID _getpid
#else
#  include <unistd.h>
#  define CHRONON3D_GETPID getpid
#endif

using namespace chronon3d;
using namespace chronon3d::media;

namespace {

struct FixtureDirectory {
    FixtureDirectory()
        : path(std::filesystem::temp_directory_path() /
               ("chronon3d-retained-replay-" + std::to_string(CHRONON3D_GETPID()))) {
        std::filesystem::create_directories(path);
    }
    ~FixtureDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    std::filesystem::path path;
};

// A 2x2 YUV420 y4m with `frame_count` distinct frames.
void write_multi_frame_y4m(const std::filesystem::path& path, int frame_count) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());
    output << "YUV4MPEG2 W2 H2 F30:1 Ip A1:1 C420jpeg\n";
    for (int k = 0; k < frame_count; ++k) {
        output << "FRAME\n";
        const unsigned char luma = static_cast<unsigned char>(16 + k);
        const unsigned char frame[6] = {luma, luma, luma, luma, 128, 128};
        output.write(reinterpret_cast<const char*>(frame), sizeof(frame));
        REQUIRE(output.good());
    }
}

// Doctest REQUIRE inside non-void helpers is awkward; use a bool result.
bool request_native_frame(NativeVideoFrameDecoder& decoder,
                          const std::filesystem::path& source, int sample_index) {
    const auto frame = decoder.decode_native_frame_at(
        source.string(), RationalTime{sample_index, {1, 30}}, 2, 2);
    return static_cast<bool>(frame);
}

} // namespace

TEST_CASE("Retained window replays repeated and regressed sample indices") {
    FixtureDirectory fixtures;
    const auto source = fixtures.path / "three_frames.y4m";
    write_multi_frame_y4m(source, 3);

    // A non-null VideoDeviceRuntime satisfies decode_native_frame_at()'s
    // native-session guard; capture-mode materialization refs the decoded
    // frame directly, so no actual GPU device is touched.
    NativeVideoFrameDecoder decoder;
    {
        auto gpu = std::make_shared<runtime::GpuRuntime>();
        std::string reason;
        auto runtime = VideoDeviceRuntime::create(0, gpu, reason);
        REQUIRE(runtime);
        decoder.set_video_runtime(std::move(runtime));
    }
    decoder.set_test_options(NativeDecoderTestOptions{
        /*enable_prefetch=*/false, /*enable_swscale=*/true,
        /*enable_frame_cache=*/true});

    const auto replays = [&] { return decoder.decode_profiling_stats().retained_replays; };

    // Sample 0 decoded for real (no retained window yet).
    REQUIRE(request_native_frame(decoder, source, 0));
    CHECK(replays() == 0);

    // Re-requesting sample 0 must be replayed from the retained window —
    // the pre-fix decoder re-seeked + re-decoded here.
    REQUIRE(request_native_frame(decoder, source, 0));
    CHECK(replays() == 1);

    // A second repeat replays again.
    REQUIRE(request_native_frame(decoder, source, 0));
    CHECK(replays() == 2);

    // A NEW sample index is a real decode and must NOT count as a replay.
    REQUIRE(request_native_frame(decoder, source, 1));
    CHECK(replays() == 2);

    // Regressing back to sample 0 (inside the retained window) replays.
    REQUIRE(request_native_frame(decoder, source, 0));
    CHECK(replays() == 3);

    // A slightly out-of-order request (0 -> 2, skipping 1) is a forward
    // decode within the same continuity, not a replay.
    REQUIRE(request_native_frame(decoder, source, 2));
    CHECK(replays() == 3);
}

TEST_CASE("Diagnose Matt Damon clip frame sequence") {
    const std::string source = "/home/pierone/src/go-master/projects/Pyt/VeloxEditing/refactored/data/media/clips/general/yt_0ElQTzSx3ec/yt_0ElQTzSx3ec_72_91_v1_matt-damon-00-01-12-00-01-31.mp4";
    if (!std::filesystem::exists(source)) {
        MESSAGE("Source file does not exist: " << source);
        return;
    }

    NativeVideoFrameDecoder decoder;
    {
        auto gpu = std::make_shared<runtime::GpuRuntime>();
        std::string reason;
        auto runtime = VideoDeviceRuntime::create(0, gpu, reason);
        REQUIRE(runtime);
        decoder.set_video_runtime(std::move(runtime));
    }
    decoder.set_test_options(NativeDecoderTestOptions{
        /*enable_prefetch=*/false, /*enable_swscale=*/true,
        /*enable_frame_cache=*/true});

    int errors = 0;
    std::vector<uint8_t> last_pixels(1920 * 10);
    std::vector<uint8_t> curr_pixels(1920 * 10);
    int identical_consecutive = 0;

    for (int frame = 0; frame < 60; ++frame) {
        auto hw_frame = decoder.decode_native_frame_at(
            source, RationalTime{frame, {1, 24}}, 1920, 1080);
        if (!hw_frame) {
            spdlog::error("FRAME FAILED: frame={}", frame);
            ++errors;
            continue;
        }
        auto* av = hw_frame.get();
        std::int64_t pts = av ? (av->best_effort_timestamp != -0x7fffffffffffffffLL ? av->best_effort_timestamp : av->pts) : -1;
        std::int64_t expected_pts = static_cast<std::int64_t>(frame) * 512;
        if (pts != expected_pts) {
            spdlog::error("FRAME MISMATCH: frame={} expected_pts={} got_pts={}",
                          frame, expected_pts, pts);
            ++errors;
        }

        // Copy top 10 lines from CUDA frame
        if (av && av->data[0]) {
            cuMemcpyDtoH(curr_pixels.data(), reinterpret_cast<CUdeviceptr>(av->data[0]), curr_pixels.size());
            if (frame > 0) {
                if (curr_pixels == last_pixels) {
                    spdlog::warn("IDENTICAL PIXELS: frame {} has identical pixels to frame {}!", frame, frame - 1);
                    ++identical_consecutive;
                } else {
                    spdlog::info("FRAME OK: frame={} pts={} pixels differ", frame, pts);
                }
            }
            last_pixels = curr_pixels;
        }
    }
    spdlog::info("Total identical consecutive frames in decoder: {}", identical_consecutive);
    CHECK(errors == 0);
}

