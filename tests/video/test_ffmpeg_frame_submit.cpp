// Per-TU coverage test for FfmpegPipeSinkInternal::validate_format + submit/submit_planar
// submit_biplanar envelope rejection paths (Phase-3 of TICKET-FFMPEG-PIPE-SINK-SPLIT).
//
// Cat-3 minimal-surface constraint (AGENTS.md): ZERO production source touched.
// Tests run at LINK-time on `chronon3d_ffmpeg_pipe_split_tests` target.
// No subprocess spawn — exercises pure logic + envelope early-out paths only.

#include <doctest/doctest.h>

#include "media/video/ffmpeg_pipe_sink.hpp"
#include "media/video/ffmpeg_pipe_sink_internal.hpp"

#include <chronon3d/media/video/video_frame.hpp>
#include <chronon3d/media/video/video_sink.hpp>

#include <cstdint>
#include <vector>

using chronon3d::media::video::FfmpegPipeSink;
using chronon3d::media::video::FfmpegPipeSinkInternal;
using chronon3d::media::video::VideoSinkConfig;
using chronon3d::media::video::VideoCodec;
using chronon3d::media::video::VideoContainer;
using chronon3d::media::video::PixelFormat;
using chronon3d::media::video::VideoFrameView;
using chronon3d::media::video::PlanarVideoFrameView;
using chronon3d::media::video::BiplanarVideoFrameView;

namespace {

VideoSinkConfig default_config() {
    VideoSinkConfig c;
    c.width = 1280;
    c.height = 720;
    c.fps_num = 30;
    c.fps_den = 1;
    c.codec = VideoCodec::H264;
    c.container = VideoContainer::Mp4;
    c.pixel_format = PixelFormat::RGBA8;
    c.output_path = "build/manual-test/per_tu_submit.mp4";
    c.overwrite = true;
    c.encode_yuv = false;
    return c;
}

}  // namespace

// ============================================================================
//                         validate_format() coverage
// ============================================================================
// FfmpegPipeSinkInternal::validate_format is the gatekeeper for all 3 submit()
// overloads (submit / submit_planar / submit_biplanar). Default-constructed
// FfmpegPipeSink has width=0/height=0/session_format_=RGBA8 → guaranteed
// mismatch for any positive VideoFrameView — perfect for negative testing
// without spawning ffmpeg.

TEST_CASE("FfmpegPipeSinkInternal: validate_format on default-constructed sink") {
    FfmpegPipeSink sink;  // width=0, height=0, session_format_=RGBA8

    SUBCASE("empty frame → rejects (zero dimensions)") {
        VideoFrameView frame{};
        CHECK_FALSE(FfmpegPipeSinkInternal::validate_format(sink, frame));
    }

    SUBCASE("positive-dim RGBA frame → rejects (width=0 != 1280)") {
        std::vector<std::uint8_t> px(1280 * 720 * 4, 0xFF);
        VideoFrameView frame{
            px.data(),  /*width*/1280u, /*height*/720u, /*stride*/1280u * 4u,
            chronon3d::media::video::PixelFormat::RGBA8
        };
        CHECK_FALSE(FfmpegPipeSinkInternal::validate_format(sink, frame));
    }

    SUBCASE("positive-dim BGRA frame → rejects (mismatch fmt + width=0)") {
        std::vector<std::uint8_t> px(1280 * 720 * 4, 0xFF);
        VideoFrameView frame{
            px.data(), 1280u, 720u, 1280u * 4u,
            chronon3d::media::video::PixelFormat::BGRA8
        };
        CHECK_FALSE(FfmpegPipeSinkInternal::validate_format(sink, frame));
    }
}

// ============================================================================
//                  submit() envelope rejection (no subprocess)
// ============================================================================
// All 3 submit overloads entry-gate on `state_ != Open` → default
// FfmpegPipeSink is state_=Closed → submission MUST return false WITHOUT
// touching process_/pipe_handle_/write_to_pipe. Verifies the safe
// envelope pre-pipe-write.

TEST_CASE("FfmpegPipeSink: submit envelope rejection on Closed session") {
    FfmpegPipeSink sink;  // state_=Closed (default ctor)
    SUBCASE("VideoFrameView submit → returns false on closed session") {
        std::vector<std::uint8_t> px(4, 0xFF);
        VideoFrameView frame{px.data(), 1u, 1u, 4u,
                             chronon3d::media::video::PixelFormat::RGBA8};
        CHECK_FALSE(sink.submit(frame));
    }

    SUBCASE("PlanarVideoFrameView submit → returns false on closed session") {
        std::vector<std::uint8_t> y(4), u(1), v(1);
        PlanarVideoFrameView frame{y.data(), u.data(), v.data(),
                                    /*width*/2u, /*height*/2u,
                                    /*y_stride*/2u, /*u_stride*/1u, /*v_stride*/1u};
        CHECK_FALSE(sink.submit_planar(frame));
    }

    SUBCASE("BiplanarVideoFrameView submit → returns false on closed session") {
        std::vector<std::uint8_t> y(4), uv(4);
        BiplanarVideoFrameView frame{y.data(), uv.data(),
                                      /*width*/2u, /*height*/2u,
                                      /*y_stride*/2u, /*uv_stride*/2u};
        CHECK_FALSE(sink.submit_biplanar(frame));
    }
}

// ============================================================================
//                           Lifecycle invariants
// ============================================================================

TEST_CASE("FfmpegPipeSink: lifetime invariants for per-TU tests") {
    SUBCASE("default-constructed sink is NOT open") {
        FfmpegPipeSink sink;
        CHECK_FALSE(sink.is_open());
    }

    SUBCASE("sink remains not-open after failed submit") {
        FfmpegPipeSink sink;
        std::vector<std::uint8_t> px(4, 0xFF);
        VideoFrameView frame{px.data(), 1u, 1u, 4u,
                             chronon3d::media::video::PixelFormat::RGBA8};
        sink.submit(frame);
        CHECK_FALSE(sink.is_open());
    }

    SUBCASE("default VideoSinkConfig reports sane defaults") {
        VideoSinkConfig c = default_config();
        CHECK(c.width == 1280);
        CHECK(c.height == 720);
        CHECK(c.fps_num == 30);
        CHECK(c.fps_den == 1);
        CHECK(c.codec == VideoCodec::H264);
        CHECK(c.container == VideoContainer::Mp4);
    }
}
