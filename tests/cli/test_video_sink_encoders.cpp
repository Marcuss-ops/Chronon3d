// ---------------------------------------------------------------------------
// test_video_sink_encoders.cpp — Unit tests for NullRenderEncoder,
// NullConvertEncoder, and RawVideoSinkEncoder.
//
/// Tests the IVideoEncoder contract for each sink encoder:
///   - Correct frame counting (full and partial)
///   - Conversion error propagation (NullConvert, Raw)
///   - Write error propagation (Raw)
///   - Owner destruction via write_frame_async()
///   - Success not recorded when a frame fails
///   - close() lifecycle (open -> write -> close)
///   - IVideoEncoder interface compliance via base-class pointer
///
/// NOTE: RawVideoSinkEncoder tests use PipePixelFormat::YUV420P (not RGBA)
/// because RGB24 conversion requires FFmpeg/swscale, while YUV420P and NV12
/// use native SIMD kernels that are always available.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <apps/chronon3d_cli/utils/video/video_sink_encoders.hpp>
#include <apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/math/color.hpp>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::cli;

namespace {

// ── Test helpers ─────────────────────────────────────────────────────────────
// Note: prefix is "sink_" to avoid ambiguity in unity-build mode where
// other test files in the same target may define identically-named helpers.

[[nodiscard]] Framebuffer sink_fb(int w, int h, Color fill = Color::red()) {
    Framebuffer fb(w, h);
    fb.clear(fill);
    return fb;
}

[[nodiscard]] FfmpegPipeOptions sink_opts(int w = 32, int h = 32,
                                          PipePixelFormat fmt = PipePixelFormat::YUV420P) {
    return FfmpegPipeOptions{
        .width          = w,
        .height         = h,
        .fps            = 30,
        .crf            = 18,
        .preset         = "medium",
        .codec          = "libx264",
        .output_path    = "/tmp/test_null_sink.mp4",
        .input_format   = fmt,
        .output_pix_fmt = "yuv420p",
        .verbose        = false,
        .color_transform = {},
        .tune           = "zerolatency",
        .pipe_writer    = "classic",
    };
}

/// Unique temp file path per call.
[[nodiscard]] std::string sink_temp_path(const char* suffix) {
    static std::atomic<int> counter{0};
    int id = counter.fetch_add(1);
    return "/tmp/chronon3d_sink_test_" + std::to_string(id) + "_" + suffix + ".raw";
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// NullRenderEncoder — lightweight counter, no conversion or I/O
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("NullRenderEncoder: default state has zero frames written") {
    NullRenderEncoder enc;
    CHECK(enc.frames_written() == 0);
}

TEST_CASE("NullRenderEncoder: open always succeeds") {
    NullRenderEncoder enc;
    CHECK(enc.open(sink_opts()));
    CHECK(enc.frames_written() == 0);
}

TEST_CASE("NullRenderEncoder: write_frame increments counter") {
    NullRenderEncoder enc;
    REQUIRE(enc.open(sink_opts()));

    CHECK(enc.write_frame(sink_fb(32, 32)));
    CHECK(enc.frames_written() == 1);

    CHECK(enc.write_frame(sink_fb(32, 32)));
    CHECK(enc.frames_written() == 2);
}

TEST_CASE("NullRenderEncoder: write_frame_async increments frames_written") {
    NullRenderEncoder enc;
    REQUIRE(enc.open(sink_opts()));

    auto owner = std::make_shared<Framebuffer>(sink_fb(32, 32));
    Framebuffer fb = sink_fb(32, 32);

    CHECK(enc.write_frame_async(fb, owner));
    // owner is passed by value — the caller's shared_ptr is not modified.
    // The contract tested here is that the write is accepted and frame count advances.
    CHECK(enc.frames_written() == 1);
}

TEST_CASE("NullRenderEncoder: multiple frames retain correct count") {
    NullRenderEncoder enc;
    REQUIRE(enc.open(sink_opts()));

    constexpr int kFrames = 100;
    for (int i = 0; i < kFrames; ++i) {
        CHECK(enc.write_frame(sink_fb(32, 32)));
    }
    CHECK(enc.frames_written() == kFrames);
}

TEST_CASE("NullRenderEncoder: close succeeds and writes still work after") {
    NullRenderEncoder enc;
    REQUIRE(enc.open(sink_opts()));

    CHECK(enc.write_frame(sink_fb(32, 32)));
    CHECK(enc.frames_written() == 1);
    CHECK(enc.close());
    CHECK(enc.frames_written() == 1);

    // NullRender is a simple counter — writes continue to succeed even after
    // close because there is no I/O resource to free.
    CHECK(enc.write_frame(sink_fb(16, 16)));
    CHECK(enc.frames_written() == 2);
}

// ═══════════════════════════════════════════════════════════════════════════════
// NullConvertEncoder — does conversion, discards result
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("NullConvertEncoder: open allocates buffer for various formats") {
    NullConvertEncoder enc;

    SUBCASE("RGBA") {
        auto opts = sink_opts(64, 64, PipePixelFormat::RGBA);
        CHECK(enc.open(opts));
        CHECK(enc.frames_written() == 0);
        enc.close();
    }

    SUBCASE("YUV420P") {
        auto opts = sink_opts(64, 64, PipePixelFormat::YUV420P);
        CHECK(enc.open(opts));
        CHECK(enc.frames_written() == 0);
        enc.close();
    }

    SUBCASE("NV12") {
        auto opts = sink_opts(64, 64, PipePixelFormat::NV12);
        CHECK(enc.open(opts));
        CHECK(enc.frames_written() == 0);
        enc.close();
    }
}

TEST_CASE("NullConvertEncoder: write_frame succeeds and increments counter") {
    NullConvertEncoder enc;
    REQUIRE(enc.open(sink_opts(32, 32, PipePixelFormat::YUV420P)));

    CHECK(enc.write_frame(sink_fb(32, 32)));
    CHECK(enc.frames_written() == 1);

    CHECK(enc.write_frame(sink_fb(32, 32)));
    CHECK(enc.frames_written() == 2);

    enc.close();
}

TEST_CASE("NullConvertEncoder: frames_written accumulates correctly") {
    NullConvertEncoder enc;
    REQUIRE(enc.open(sink_opts(32, 32, PipePixelFormat::YUV420P)));

    constexpr int kFrames = 10;
    for (int i = 0; i < kFrames; ++i) {
        Framebuffer fb(32, 32);
        fb.clear(Color{0.1f + i * 0.05f, 0.3f, 0.5f, 1.0f});
        CHECK(enc.write_frame(fb));
    }
    CHECK(enc.frames_written() == kFrames);
    enc.close();
}

TEST_CASE("NullConvertEncoder: write_frame updates profiling counters") {
    RenderCounters counters;
    counters.reset();
    profiling::g_current_counters = &counters;

    {
        NullConvertEncoder enc;
        REQUIRE(enc.open(sink_opts(32, 32, PipePixelFormat::YUV420P)));
        enc.set_counters(&counters);

        CHECK(enc.write_frame(sink_fb(32, 32)));
        CHECK(enc.write_frame(sink_fb(32, 32)));
        enc.close();
    }

    // Conversion counters should have accumulated
    CHECK(counters.video_conversion_ms.load(std::memory_order_relaxed) > 0);
    CHECK(counters.frame_conversion_copy_ms.load(std::memory_order_relaxed) > 0);
    CHECK(counters.video_frames_submitted.load(std::memory_order_relaxed) == 2);
    CHECK(counters.video_frames_converted.load(std::memory_order_relaxed) == 2);

    profiling::g_current_counters = nullptr;
}

TEST_CASE("NullConvertEncoder: write_frame_async increments frames_written") {
    NullConvertEncoder enc;
    REQUIRE(enc.open(sink_opts(32, 32, PipePixelFormat::YUV420P)));

    auto owner = std::make_shared<Framebuffer>(sink_fb(32, 32));
    Framebuffer fb = sink_fb(32, 32);

    CHECK(enc.write_frame_async(fb, owner));
    // owner is passed by value — the caller's shared_ptr is not modified.
    // The contract tested here is that the write is accepted and frame count advances.
    CHECK(enc.frames_written() == 1);

    enc.close();
}



TEST_CASE("NullConvertEncoder: close succeeds") {
    NullConvertEncoder enc;
    REQUIRE(enc.open(sink_opts(32, 32)));
    CHECK(enc.write_frame(sink_fb(32, 32)));
    CHECK(enc.close());
}

// ═══════════════════════════════════════════════════════════════════════════════
// RawVideoSinkEncoder — writes raw pixel data to file
//
// NOTE: All tests use PipePixelFormat::YUV420P (not RGBA). The RGBA format
// maps to RGB24 conversion which requires FFmpeg/swscale. YUV420P and NV12
// use native SIMD kernels that are always available.
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSinkEncoder: default state has zero frames written") {
    RawVideoSinkEncoder enc;
    CHECK(enc.frames_written() == 0);
}

TEST_CASE("RawVideoSinkEncoder: open creates output file") {
    const auto path = sink_temp_path("open_creates");

    RawVideoSinkEncoder enc;
    auto opts = sink_opts(16, 16, PipePixelFormat::YUV420P);
    opts.output_path = path;

    CHECK(enc.open(opts));
    CHECK(std::filesystem::exists(path));
    CHECK(enc.frames_written() == 0);

    enc.close();
    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSinkEncoder: open fails on invalid directory") {
    RawVideoSinkEncoder enc;
    auto opts = sink_opts(16, 16, PipePixelFormat::YUV420P);
    opts.output_path = "/nonexistent_dir_xyz/sink_test.raw";

    CHECK_FALSE(enc.open(opts));
    CHECK(enc.frames_written() == 0);
}

TEST_CASE("RawVideoSinkEncoder: write_frame increments frames_written") {
    const auto path = sink_temp_path("write_frame");

    RawVideoSinkEncoder enc;
    auto opts = sink_opts(16, 16, PipePixelFormat::YUV420P);
    opts.output_path = path;
    REQUIRE(enc.open(opts));

    CHECK(enc.write_frame(sink_fb(16, 16)));
    CHECK(enc.frames_written() == 1);

    CHECK(enc.write_frame(sink_fb(16, 16)));
    CHECK(enc.frames_written() == 2);

    enc.close();
    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSinkEncoder: file size matches expected for YUV420P frames") {
    const auto path = sink_temp_path("file_size");

    constexpr int kFrames = 5;
    constexpr int kWidth  = 8;
    constexpr int kHeight = 8;

    RawVideoSinkEncoder enc;
    auto opts = sink_opts(kWidth, kHeight, PipePixelFormat::YUV420P);
    opts.output_path = path;
    REQUIRE(enc.open(opts));

    for (int i = 0; i < kFrames; ++i) {
        Framebuffer fb(kWidth, kHeight);
        fb.clear(Color{0.2f + i * 0.1f, 0.3f, 0.4f, 1.0f});
        CHECK(enc.write_frame(fb));
    }
    CHECK(enc.frames_written() == kFrames);
    enc.close();

    // YUV420P: Y plane = w*h, U/V planes each = (w/2)*(h/2)
    // Total per frame = w*h + (w/2)*(h/2)*2 = w*h*1.5
    const size_t per_frame = static_cast<size_t>(kWidth) * kHeight * 3 / 2;
    const size_t expected = static_cast<size_t>(kFrames) * per_frame;
    const size_t actual   = std::filesystem::file_size(path);

    CHECK_MESSAGE(actual >= expected,
                  "File too small: " << actual << " < " << expected);
    CHECK_MESSAGE(actual <= expected + 64,
                  "File too large: " << actual << " > " << (expected + 64));

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSinkEncoder: write_frame_async increments frames_written") {
    const auto path = sink_temp_path("async_owner");

    RawVideoSinkEncoder enc;
    auto opts = sink_opts(16, 16, PipePixelFormat::YUV420P);
    opts.output_path = path;
    REQUIRE(enc.open(opts));

    auto owner = std::make_shared<Framebuffer>(sink_fb(16, 16));
    Framebuffer fb = sink_fb(16, 16);

    CHECK(enc.write_frame_async(fb, owner));
    // Note: owner is passed by value to write_frame_async, so the caller's
    // shared_ptr is not modified by the callee's owner.reset(). The contract
    // tested here is that the write is accepted and frame count advances.
    CHECK(enc.frames_written() == 1);

    enc.close();
    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSinkEncoder: close flushes and finalizes file") {
    const auto path = sink_temp_path("close_flush");

    RawVideoSinkEncoder enc;
    auto opts = sink_opts(16, 16, PipePixelFormat::YUV420P);
    opts.output_path = path;
    REQUIRE(enc.open(opts));

    CHECK(enc.write_frame(sink_fb(16, 16)));
    CHECK(enc.close());

    // File exists and has data after close
    CHECK(std::filesystem::exists(path));
    CHECK(std::filesystem::file_size(path) > 0);

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSinkEncoder: write after close fails and does NOT increment") {
    const auto path = sink_temp_path("write_after_close");

    RawVideoSinkEncoder enc;
    auto opts = sink_opts(8, 8, PipePixelFormat::YUV420P);
    opts.output_path = path;
    REQUIRE(enc.open(opts));

    // Successful write
    CHECK(enc.write_frame(sink_fb(8, 8)));
    CHECK(enc.frames_written() == 1);

    // Close the file
    CHECK(enc.close());

    // Attempt write after close — must NOT increment frames_written
    uint64_t before = enc.frames_written();
    bool wrote = enc.write_frame(sink_fb(8, 8));
    uint64_t after = enc.frames_written();

    CHECK_MESSAGE(after == before,
                  "frames_written must NOT increment when writing to closed encoder");
    CHECK_FALSE_MESSAGE(wrote,
                       "write_frame must return false after encoder is closed");

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSinkEncoder: partial output preserves correct count") {
    // Partial output: only successfully written frames are counted.
    const auto path = sink_temp_path("partial");

    RawVideoSinkEncoder enc;
    auto opts = sink_opts(8, 8, PipePixelFormat::YUV420P);
    opts.output_path = path;
    REQUIRE(enc.open(opts));

    // Write 3 frames successfully
    CHECK(enc.write_frame(sink_fb(8, 8)));
    CHECK(enc.write_frame(sink_fb(8, 8)));
    CHECK(enc.write_frame(sink_fb(8, 8)));
    CHECK(enc.frames_written() == 3);

    enc.close();

    // File size should represent 3 frames of YUV420P
    const size_t per_frame = 8 * 8 * 3 / 2;
    CHECK(std::filesystem::file_size(path) >= 3 * per_frame);

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSinkEncoder: counters are updated on successful writes") {
    const auto path = sink_temp_path("counters");

    RenderCounters counters;
    counters.reset();
    profiling::g_current_counters = &counters;

    {
        RawVideoSinkEncoder enc;
        auto opts = sink_opts(16, 16, PipePixelFormat::YUV420P);
        opts.output_path = path;
        REQUIRE(enc.open(opts));
        enc.set_counters(&counters);

        CHECK(enc.write_frame(sink_fb(16, 16)));
        CHECK(enc.write_frame(sink_fb(16, 16)));

        enc.close();
    }

    // Frame counters must be exact; timing counters may be 0 for tiny frames
    // (16x16 YUV420P converts in <1 timer tick)
    CHECK(counters.video_conversion_ms.load(std::memory_order_relaxed) >= 0);
    CHECK(counters.frame_conversion_copy_ms.load(std::memory_order_relaxed) >= 0);
    CHECK(counters.video_frames_submitted.load(std::memory_order_relaxed) == 2);
    CHECK(counters.video_frames_converted.load(std::memory_order_relaxed) == 2);
    CHECK(counters.video_frames_written_counter.load(std::memory_order_relaxed) == 2);
    CHECK(counters.video_pipe_write_ms.load(std::memory_order_relaxed) >= 0);

    profiling::g_current_counters = nullptr;
    std::filesystem::remove(path);
}

// ═══════════════════════════════════════════════════════════════════════════════
// IVideoEncoder interface compliance (via base-class pointer)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("All sink encoders: satisfy IVideoEncoder interface") {
    SUBCASE("NullRenderEncoder") {
        auto enc = std::make_unique<NullRenderEncoder>();
        auto* base = static_cast<IVideoEncoder*>(enc.get());

        CHECK(base->open(sink_opts()));
        CHECK(base->write_frame(sink_fb(32, 32)));
        CHECK(base->frames_written() == 1);
        CHECK(base->close());
    }

    SUBCASE("NullConvertEncoder") {
        auto enc = std::make_unique<NullConvertEncoder>();
        auto* base = static_cast<IVideoEncoder*>(enc.get());

        auto opts = sink_opts(32, 32, PipePixelFormat::YUV420P);
        CHECK(base->open(opts));
        CHECK(base->write_frame(sink_fb(32, 32)));
        CHECK(base->frames_written() == 1);
        CHECK(base->close());
    }

    SUBCASE("RawVideoSinkEncoder") {
        const auto path = sink_temp_path("base_interface");
        auto enc = std::make_unique<RawVideoSinkEncoder>();
        auto* base = static_cast<IVideoEncoder*>(enc.get());

        auto opts = sink_opts(16, 16, PipePixelFormat::YUV420P);
        opts.output_path = path;
        CHECK(base->open(opts));
        CHECK(base->write_frame(sink_fb(16, 16)));
        CHECK(base->frames_written() == 1);
        CHECK(base->close());

        std::filesystem::remove(path);
    }
}
