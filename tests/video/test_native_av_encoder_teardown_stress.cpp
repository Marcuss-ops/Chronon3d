#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include "../../apps/chronon3d_cli/utils/video/native_av_encoder.hpp"

#include <string>

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
