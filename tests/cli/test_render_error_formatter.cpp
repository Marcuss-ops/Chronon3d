#include <doctest/doctest.h>

#include "utils/common/render_error_formatter.hpp"

#include <chronon3d/render_graph/render_backend.hpp>

#include <string>
#include <utility>

namespace cli = chronon3d::cli;
namespace graph = chronon3d::graph;

namespace {

graph::NodeExecutionError error_with(std::string message) {
    return graph::NodeExecutionError{
        graph::RenderBackendErrorCode::ExecutionFailure,
        "fixture-layer",
        std::move(message),
    };
}

} // namespace

TEST_CASE("render error formatter maps the canonical diagnostics taxonomy") {
    struct Case {
        const char* message;
        const char* expected_code;
    };

    const Case cases[] = {
        {"prepare_text_run: failed to load font face for fonts/Missing.ttf",
         "TEXT_FONT_NOT_FOUND"},
        {"AssetResolutionFailure: asset 'images/missing.png'",
         "ASSET_NOT_FOUND"},
        {"DecodeFailed: failed to decode image 'images/corrupt.png'",
         "DECODE_FAILED"},
        {"InvalidCameraProgram: invalid camera descriptor",
         "INVALID_CAMERA_DESCRIPTOR"},
        {"CameraTargetNotFound: camera target not found",
         "CAMERA_TARGET_NOT_FOUND"},
        {"MaxDimensionExceeded: frame dimension exceeded",
         "FRAME_DIMENSION_EXCEEDED"},
        {"InsufficientMemory: memory budget exceeded",
         "MEMORY_BUDGET_EXCEEDED"},
        {"OutputOpenFailed: failed to open output 'out/frame.png'",
         "OUTPUT_OPEN_FAILED"},
        {"EncoderFailed: video encoder failed",
         "VIDEO_ENCODER_FAILED"},
        {"InvalidRange: invalid frame range",
         "INVALID_TIME_RANGE"},
        {"unknown composition 'Missing'", "UNKNOWN_COMPOSITION"},
        {"could not create composition 'Broken'", "COMPOSITION_CREATE_FAILED"},
    };

    for (const auto& item : cases) {
        CAPTURE(item.message);
        const auto error = error_with(item.message);
        CHECK(std::string(cli::render_error_detail::stable_cli_code(error)) ==
              item.expected_code);
    }
}

TEST_CASE("render error formatter extracts actionable asset paths") {
    SUBCASE("font path") {
        const auto path = cli::render_error_detail::extract_asset(
            "prepare_text_run: failed to load font face for fonts/Inter-Bold.ttf");
        CHECK(path == "fonts/Inter-Bold.ttf");
    }

    SUBCASE("quoted asset path") {
        const auto path = cli::render_error_detail::extract_asset(
            "AssetResolutionFailure: asset 'images/hero.png'");
        CHECK(path == "images/hero.png");
    }

    SUBCASE("quoted output path") {
        const auto path = cli::render_error_detail::extract_asset(
            "failed to save frame to output '/proc/frame.png' as png");
        CHECK(path == "/proc/frame.png");
    }
}

TEST_CASE("render error formatter keeps backend fallback codes") {
    const graph::NodeExecutionError unsupported{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "fixture-layer",
        "capability is unavailable",
    };
    const graph::NodeExecutionError invalid{
        graph::RenderBackendErrorCode::InvalidInput,
        "fixture-layer",
        "generic invalid input",
    };
    const graph::NodeExecutionError execution{
        graph::RenderBackendErrorCode::ExecutionFailure,
        "fixture-layer",
        "generic execution failure",
    };

    CHECK(std::string(cli::render_error_detail::stable_cli_code(unsupported)) ==
          "RENDER_UNSUPPORTED_CAPABILITY");
    CHECK(std::string(cli::render_error_detail::stable_cli_code(invalid)) ==
          "RENDER_INVALID_INPUT");
    CHECK(std::string(cli::render_error_detail::stable_cli_code(execution)) ==
          "RENDER_EXECUTION_FAILURE");
}
