#include <doctest/doctest.h>
#include <apps/chronon3d_cli/utils/cli_mappers.hpp>
#include <chronon3d/backends/video/video_encoder.hpp>

using namespace chronon3d;
using namespace chronon3d::cli;
using namespace chronon3d::video;

TEST_CASE("parse_hardware_encoder maps options accurately") {
    // None / Software fallback
    CHECK(parse_hardware_encoder("none") == HardwareEncoder::None);
    CHECK(parse_hardware_encoder("software") == HardwareEncoder::None);
    CHECK(parse_hardware_encoder("off") == HardwareEncoder::None);
    CHECK(parse_hardware_encoder("NONE") == HardwareEncoder::None);

    // Auto
    CHECK(parse_hardware_encoder("auto") == HardwareEncoder::Auto);
    CHECK(parse_hardware_encoder("AUTO") == HardwareEncoder::Auto);

    // Specific backends
    CHECK(parse_hardware_encoder("nvenc") == HardwareEncoder::Nvenc);
    CHECK(parse_hardware_encoder("NVENC") == HardwareEncoder::Nvenc);
    CHECK(parse_hardware_encoder("qsv") == HardwareEncoder::Qsv);
    CHECK(parse_hardware_encoder("QSV") == HardwareEncoder::Qsv);
    CHECK(parse_hardware_encoder("videotoolbox") == HardwareEncoder::VideoToolbox);
    CHECK(parse_hardware_encoder("vt") == HardwareEncoder::VideoToolbox);
    CHECK(parse_hardware_encoder("amf") == HardwareEncoder::Amf);

    // Unknown options should return std::nullopt
    CHECK(!parse_hardware_encoder("unknown").has_value());
    CHECK(!parse_hardware_encoder("").has_value());
}

TEST_CASE("VideoEncodeOptions default hardware field") {
    VideoEncodeOptions options;
    CHECK(options.hardware == HardwareEncoder::None);
}
