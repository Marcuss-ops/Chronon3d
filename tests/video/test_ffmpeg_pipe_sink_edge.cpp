#include <doctest/doctest.h>
#include "test_ffmpeg_pipe_sink_helpers.hpp"
#include "src/media/video/ffmpeg_pipe_sink.hpp"
#include <csignal>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <sys/types.h>
#include <signal.h>
#endif

using namespace chronon3d;
using namespace chronon3d::media::video;
using namespace chronon3d::media::video::test_ffpipe;

// ═════════════════════════════════════════════════════════════════════════════
//  Validation at submit time (requires ffmpeg to reach Open)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: submit with null data fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("null_sub.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    VideoFrameView fv;
    fv.data         = nullptr;
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);
    CHECK(sink.frames_submitted() == 0);

    sink.close();
}

TEST_CASE("FfmpegPipeSink: submit with mismatched format fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("bad_fmt.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::YUV420P, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16 * 4);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

TEST_CASE("FfmpegPipeSink: submit with mismatched dimensions fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("bad_dims.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(32 * 32 * 4);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 32;
    fv.height       = 32;
    fv.pixel_format = PixelFormat::RGBA8;

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

TEST_CASE("FfmpegPipeSink: packed YUV with non-zero stride fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("yuv_stride.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::YUV420P, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16 * 2);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::YUV420P;
    fv.stride_bytes = 32;

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

TEST_CASE("FfmpegPipeSink: submit_planar with wrong session format fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("planar_wrong_fmt.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16);
    PlanarVideoFrameView pfv;
    pfv.y_data = buf.data();
    pfv.u_data = buf.data();
    pfv.v_data = buf.data();
    pfv.width  = 16;
    pfv.height = 16;

    CHECK_FALSE(sink.submit_planar(pfv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

TEST_CASE("FfmpegPipeSink: submit_biplanar with null uv_data fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("null_uv.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::NV12, path);
    REQUIRE(sink.open(cfg));

    BiplanarVideoFrameView bfv;
    bfv.y_data  = reinterpret_cast<const void*>(0x1);
    bfv.uv_data = nullptr;
    bfv.width   = 16;
    bfv.height  = 16;

    CHECK_FALSE(sink.submit_biplanar(bfv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

TEST_CASE("FfmpegPipeSink: submit_planar with mismatched dimensions fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("planar_dims.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::YUV420P, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(32 * 32);
    PlanarVideoFrameView pfv;
    pfv.y_data = buf.data();
    pfv.u_data = buf.data();
    pfv.v_data = buf.data();
    pfv.width  = 32;
    pfv.height = 32;

    CHECK_FALSE(sink.submit_planar(pfv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

TEST_CASE("FfmpegPipeSink: submit_biplanar with mismatched dimensions fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("biplanar_dims.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::NV12, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(32 * 32);
    BiplanarVideoFrameView bfv;
    bfv.y_data  = buf.data();
    bfv.uv_data = buf.data();
    bfv.width   = 32;
    bfv.height  = 32;

    CHECK_FALSE(sink.submit_biplanar(bfv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

TEST_CASE("FfmpegPipeSink: packed stride too small fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("stride_small.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(32 * 16);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;
    fv.stride_bytes = 32;

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Broken pipe tests (requires ffmpeg)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: kill ffmpeg mid-stream sets pipeline failure") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("killed.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16 * 4);
    fill_pattern(buf.data(), buf.size());
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;
    CHECK(sink.submit(fv));

    int pid = sink.ffmpeg_pid();
    REQUIRE(pid > 0);
    ::kill(pid, SIGKILL);

    bool any_failed = false;
    for (int attempt = 0; attempt < 30; ++attempt) {
        fill_pattern(buf.data(), buf.size(), static_cast<uint8_t>(attempt));
        if (!sink.submit(fv)) {
            any_failed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    CHECK(any_failed);
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

TEST_CASE("FfmpegPipeSink: close after ffmpeg killed returns Failed state") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("killed_close.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16 * 4);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;
    sink.submit(fv);

    int pid = sink.ffmpeg_pid();
    REQUIRE(pid > 0);
    ::kill(pid, SIGKILL);

    for (int attempt = 0; attempt < 30; ++attempt) {
        if (!sink.submit(fv)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    REQUIRE(sink.state() == VideoSinkState::Failed);

    CHECK_FALSE(sink.close());
    CHECK(sink.state() == VideoSinkState::Failed);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Exit code tests (requires ffmpeg)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: successful close collects exit code 0") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("exit0.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16 * 4);
    fill_pattern(buf.data(), buf.size());
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;
    sink.submit(fv);

    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Overwrite tests (requires ffmpeg)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: path with spaces works correctly") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("path with spaces.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16 * 4);
    fill_pattern(buf.data(), buf.size());
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;
    CHECK(sink.submit(fv));
    CHECK(sink.close());
    CHECK(file_nonempty(path));
}

TEST_CASE("FfmpegPipeSink: overwrite=true succeeds when file exists") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("ow_true.mp4");
    TempFile cleanup(path);

    {
        std::ofstream f(path, std::ios::binary);
        f.put('x');
    }
    REQUIRE(std::filesystem::exists(path));

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path, true);
    CHECK(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Open);

    std::vector<uint8_t> buf(16 * 16 * 4);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;
    sink.submit(fv);
    sink.close();
    CHECK(file_nonempty(path));
}

TEST_CASE("FfmpegPipeSink: nonexistent output directory fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8,
                           "/nonexistent_dir_xyz_123/test.mp4");
    bool opened = sink.open(cfg);
    if (opened) {
        std::vector<uint8_t> buf(16 * 16 * 4);
        VideoFrameView fv;
        fv.data         = buf.data();
        fv.width        = 16;
        fv.height       = 16;
        fv.pixel_format = PixelFormat::RGBA8;
        bool submit_ok = sink.submit(fv);
        bool close_ok  = sink.close();
        CHECK((!submit_ok || !close_ok));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Write timeout tests (requires ffmpeg)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: write_timeout=0 uses blocking write (normal)") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("tmout0.mp4");
    TempFile cleanup(path);
    constexpr int W = 16, H = 16, N = 3;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    FfmpegPipeSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    cfg.transport.write_timeout = std::chrono::milliseconds(0);
    REQUIRE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Open);

    for (int i = 0; i < N; ++i) {
        std::vector<uint8_t> buf(frame_bytes);
        fill_pattern(buf.data(), frame_bytes, static_cast<uint8_t>(i * 33));
        VideoFrameView fv;
        fv.data         = buf.data();
        fv.width        = W;
        fv.height       = H;
        fv.pixel_format = PixelFormat::RGBA8;
        CHECK(sink.submit(fv));
    }
    CHECK(sink.frames_submitted() == N);
    CHECK(sink.close());
    CHECK(file_nonempty(path));
}

TEST_CASE("FfmpegPipeSink: write_timeout=5ms succeeds for normal writes") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("tmout5.mp4");
    TempFile cleanup(path);
    constexpr int W = 16, H = 16;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    FfmpegPipeSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    cfg.transport.write_timeout = std::chrono::milliseconds(5);
    REQUIRE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Open);

    for (int i = 0; i < 3; ++i) {
        std::vector<uint8_t> buf(frame_bytes);
        fill_pattern(buf.data(), frame_bytes, static_cast<uint8_t>(i));
        VideoFrameView fv;
        fv.data         = buf.data();
        fv.width        = W;
        fv.height       = H;
        fv.pixel_format = PixelFormat::RGBA8;
        CHECK(sink.submit(fv));
    }
    CHECK(sink.frames_submitted() == 3);
    CHECK(sink.close());
    CHECK(file_nonempty(path));
}

TEST_CASE("FfmpegPipeSink: write_timeout=10ms with killed child fails fast") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("tmout_kill.mp4");
    TempFile cleanup(path);
    constexpr int W = 16, H = 16;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    FfmpegPipeSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    cfg.transport.write_timeout = std::chrono::milliseconds(10);
    REQUIRE(sink.open(cfg));

    {
        std::vector<uint8_t> buf(frame_bytes);
        fill_pattern(buf.data(), frame_bytes);
        VideoFrameView fv;
        fv.data         = buf.data();
        fv.width        = W;
        fv.height       = H;
        fv.pixel_format = PixelFormat::RGBA8;
        CHECK(sink.submit(fv));
    }

    int pid = sink.ffmpeg_pid();
    REQUIRE(pid > 0);
    ::kill(pid, SIGKILL);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto t0 = std::chrono::steady_clock::now();
    {
        std::vector<uint8_t> buf(frame_bytes);
        fill_pattern(buf.data(), frame_bytes);
        VideoFrameView fv;
        fv.data         = buf.data();
        fv.width        = W;
        fv.height       = H;
        fv.pixel_format = PixelFormat::RGBA8;
        CHECK_FALSE(sink.submit(fv));
    }
    auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    CHECK(sink.state() == VideoSinkState::Failed);
    CHECK(elapsed < 500);

    sink.close();
}
