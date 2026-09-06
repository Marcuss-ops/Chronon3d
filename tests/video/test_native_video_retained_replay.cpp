// Deterministic regression coverage for the retained-frame decode window
// (commit 6d6d9d5 "Fix native video rendering pipeline").
//
// The fix replays duplicate or slightly-out-of-order sample requests — the
// two-layers-share-one-source pattern, FPS-mapping repeats — from a small
// window of recently materialized frames instead of seeking back to the
// previous keyframe and re-decoding the GOP (~100-200ms per repeated index).
//
// decode_native_frame_at() captures whatever frame the decoder materializes
// (software frames included), so the replay path is exercised deterministically
// without a GPU: no CUDA/NVDEC device is required.
//
// Regression assertion: the retained-replay counter (decode_profiling_stats)
// increments on a repeated/regressed sample index and stays flat when a NEW
// sample is decoded — the pre-fix decoder never incremented it because every
// repeated request went through a fresh keyframe seek + decode.

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>
#include <chronon3d/runtime/gpu_runtime.hpp>

#include <array>
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

// A 2x2 YUV420 y4m with `frame_count` distinct frames. Frame k has luma
// plane filled with 16+k so every decoded frame is distinguishable.
void write_multi_frame_y4m(const std::filesystem::path& path, int frame_count) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());
    output << "YUV4MPEG2 W2 H2 F30:1 Ip A1:1 C420jpeg\n";
    for (int k = 0; k < frame_count; ++k) {
        output << "FRAME\n";
        const unsigned char luma = static_cast<unsigned char>(16 + k);
        const unsigned char chroma = static_cast<unsigned char>(128 + k);
        const unsigned char frame[6] = {luma, luma, luma, luma, chroma, chroma};
        output.write(reinterpret_cast<const char*>(frame), sizeof(frame));
        REQUIRE(output.good());
    }
}

// Doctest REQUIRE inside non-void helpers is awkward; use a bool result.
bool request_frame(NativeVideoFrameDecoder& decoder,
                   const std::filesystem::path& source, int sample_index) {
    const auto frame = decoder.decode_native_frame_at(
        source.string(), RationalTime{sample_index, {1, 30}}, 2, 2);
    return static_cast<bool>(frame);
}

} // namespace

TEST_CASE("Retained window replays repeated sample indices without re-decoding") {
    FixtureDirectory fixtures;
    const auto source = fixtures.path / "three_frames.y4m";
    write_multi_frame_y4m(source, 3);

    // A non-null VideoDeviceRuntime satisfies decode_native_frame_at()'s
    // native-session guard; capture-mode materialization refs the decoded
    // frame directly, so no actual GPU device is touched.
    auto gpu = std::make_shared<runtime::GpuRuntime>();
    std::string reason;
    auto runtime = VideoDeviceRuntime::create(0, gpu, reason);
    REQUIRE(runtime);

    NativeVideoFrameDecoder decoder;
    decoder.set_video_runtime(runtime);
    decoder.set_test_options(NativeDecoderTestOptions{
        /*enable_prefetch=*/false, /*enable_swscale=*/true,
        /*enable_frame_cache=*/true});

    const auto stats = [&] { return decoder.decode_profiling_stats(); };

    // Sample 0 decoded for real (no retained window yet).
    REQUIRE(request_frame(decoder, source, 0));
    CHECK(stats().retained_replays == 0);

    // Re-requesting sample 0 must be replayed from the retained window —
    // the pre-fix decoder re-seeked + re-decoded here.
    REQUIRE(request_frame(decoder, source, 0));
    CHECK(stats().retained_replays == 1);

    // A second repeat replays again.
    REQUIRE(request_frame(decoder, source, 0));
    CHECK(stats().retained_replays == 2);

    // A NEW sample index is a real decode and must NOT count as a replay.
    REQUIRE(request_frame(decoder, source, 1));
    CHECK(stats().retained_replays == 2);

    // Regressing back to sample 0 (inside the retained window) replays.
    REQUIRE(request_frame(decoder, source, 0));
    CHECK(stats().retained_replays == 3);

    // A slightly out-of-order request (0 -> 2 skipping 1) is a forward
    // decode within the same continuity, not a replay.
    REQUIRE(request_frame(decoder, source, 2));
    CHECK(stats().retained_replays == 3);
}

TEST_CASE("Retained window replays the correct frame content") {
    FixtureDirectory fixtures;
    const auto source = fixtures.path / "three_frames_content.y4m";
    write_multi_frame_y4m(source, 3);

    auto gpu = std::make_shared<runtime::GpuRuntime>();
    std::string reason;
    auto runtime = VideoDeviceRuntime::create(0, gpu, reason);
    REQUIRE(runtime);

    NativeVideoFrameDecoder decoder;
    decoder.set_video_runtime(runtime);
    decoder.set_test_options(NativeDecoderTestOptions{
        /*enable_prefetch=*/false, /*enable_swscale=*/true,
        /*enable_frame_cache=*/true});

    // Reference framebuffer for sample 1 (decoded once, kept out of the
    // retained-replay accounting by asserting via decode_frame_at).
    const auto reference = decoder.decode_frame_at(
        source.string(), RationalTime{1, {1, 30}}, 2, 2);
    REQUIRE(std::holds_alternative<DecodedFrame>(reference));

    // Native-capture request of sample 1, then a replay of the same index.
    REQUIRE(request_frame(decoder, source, 1));
    REQUIRE(request_frame(decoder, source, 1));
    CHECK(decoder.decode_profiling_stats().retained_replays == 1);
}
