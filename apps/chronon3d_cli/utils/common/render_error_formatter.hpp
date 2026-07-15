#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

#include <initializer_list>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::cli {

namespace render_error_detail {

[[nodiscard]] inline bool contains(
    std::string_view text,
    std::string_view needle) noexcept
{
    return text.find(needle) != std::string_view::npos;
}

[[nodiscard]] inline bool contains_any(
    std::string_view text,
    std::initializer_list<std::string_view> needles) noexcept
{
    for (const auto needle : needles) {
        if (contains(text, needle)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline std::string extract_quoted_value_after(
    std::string_view message,
    std::string_view marker)
{
    const auto marker_pos = message.find(marker);
    if (marker_pos == std::string_view::npos) {
        return {};
    }

    const auto begin = marker_pos + marker.size();
    const auto end = message.find_first_of("'\"", begin);
    if (end == std::string_view::npos || end <= begin) {
        return {};
    }
    return std::string(message.substr(begin, end - begin));
}

[[nodiscard]] inline std::string extract_asset(std::string_view message) {
    constexpr std::string_view span_marker =
        "prepare_text_run: failed to load font face for span ('";
    if (const auto pos = message.find(span_marker); pos != std::string_view::npos) {
        const auto begin = pos + span_marker.size();
        const auto end = message.find("',", begin);
        if (end != std::string_view::npos) {
            return std::string(message.substr(begin, end - begin));
        }
    }

    constexpr std::string_view single_marker =
        "prepare_text_run: failed to load font face for ";
    if (const auto pos = message.find(single_marker); pos != std::string_view::npos) {
        const auto begin = pos + single_marker.size();
        return std::string(message.substr(begin));
    }

    constexpr std::string_view quoted_markers[] = {
        "asset '",
        "asset \"",
        "file '",
        "file \"",
        "path '",
        "path \"",
        "image '",
        "image \"",
        "video '",
        "video \"",
        "audio '",
        "audio \"",
        "font '",
        "font \"",
        "output '",
        "output \"",
    };

    for (const auto marker : quoted_markers) {
        if (auto value = extract_quoted_value_after(message, marker); !value.empty()) {
            return value;
        }
    }

    return {};
}

[[nodiscard]] inline bool is_font_not_found(
    const graph::NodeExecutionError& error) noexcept
{
    return contains_any(error.message, {
        "failed to load font face",
        "empty layout.font.font_path",
        "MissingFontEngine",
        "font not found",
    });
}

[[nodiscard]] inline bool is_unknown_composition(
    const graph::NodeExecutionError& error) noexcept
{
    return contains(error.message, "unknown composition");
}

[[nodiscard]] inline bool is_composition_create_failure(
    const graph::NodeExecutionError& error) noexcept
{
    return contains(error.message, "could not create composition");
}

[[nodiscard]] inline bool is_asset_not_found(
    const graph::NodeExecutionError& error) noexcept
{
    return contains_any(error.message, {
        "AssetResolutionFailure",
        "asset not found",
        "missing asset",
        "failed to resolve asset",
        "asset resolution failed",
        "image not found",
        "No such file or directory",
    });
}

[[nodiscard]] inline bool is_decode_failed(
    const graph::NodeExecutionError& error) noexcept
{
    return contains_any(error.message, {
        "DecodeFailed",
        "decode failed",
        "failed to decode",
        "decoder failed",
        "corrupt image",
        "corrupt media",
        "invalid image data",
    });
}

[[nodiscard]] inline bool is_invalid_camera_descriptor(
    const graph::NodeExecutionError& error) noexcept
{
    return contains_any(error.message, {
        "InvalidCameraProgram",
        "invalid camera descriptor",
        "invalid camera program",
        "camera descriptor is invalid",
    });
}

[[nodiscard]] inline bool is_camera_target_not_found(
    const graph::NodeExecutionError& error) noexcept
{
    return contains_any(error.message, {
        "CameraTargetNotFound",
        "camera target not found",
        "camera target is missing",
        "TargetNotFound",
    });
}

[[nodiscard]] inline bool is_frame_dimension_exceeded(
    const graph::NodeExecutionError& error) noexcept
{
    return contains_any(error.message, {
        "MaxDimensionExceeded",
        "frame dimension exceeded",
        "dimensions exceed",
        "exceeds maximum dimension",
        "frame is too large",
    });
}

[[nodiscard]] inline bool is_memory_budget_exceeded(
    const graph::NodeExecutionError& error) noexcept
{
    return contains_any(error.message, {
        "InsufficientMemory",
        "memory budget exceeded",
        "memory budget exhausted",
        "out of memory",
        "allocation failed",
    });
}

[[nodiscard]] inline bool is_output_open_failed(
    const graph::NodeExecutionError& error) noexcept
{
    return contains_any(error.message, {
        "OutputOpenFailed",
        "DirectoryNotWritable",
        "failed to open output",
        "cannot open output",
        "output path is not writable",
        "failed to save frame",
    });
}

[[nodiscard]] inline bool is_video_encoder_failed(
    const graph::NodeExecutionError& error) noexcept
{
    return contains_any(error.message, {
        "EncoderFailed",
        "CodecNotFound",
        "video encoder failed",
        "failed to initialize encoder",
        "failed to start ffmpeg",
        "ffmpeg exited",
    });
}

[[nodiscard]] inline bool is_invalid_time_range(
    const graph::NodeExecutionError& error) noexcept
{
    return contains_any(error.message, {
        "InvalidRange",
        "invalid time range",
        "invalid frame range",
        "empty frame range",
        "range start exceeds range end",
    });
}

[[nodiscard]] inline bool is_user_layer_name(std::string_view node_name) noexcept {
    return !node_name.empty() &&
           node_name != "composition_registry" &&
           node_name != "frame_graph" &&
           node_name != "render";
}

[[nodiscard]] inline const char* stable_cli_code(
    const graph::NodeExecutionError& error) noexcept
{
    if (is_font_not_found(error)) {
        return "TEXT_FONT_NOT_FOUND";
    }
    if (is_unknown_composition(error)) {
        return "UNKNOWN_COMPOSITION";
    }
    if (is_composition_create_failure(error)) {
        return "COMPOSITION_CREATE_FAILED";
    }
    if (is_camera_target_not_found(error)) {
        return "CAMERA_TARGET_NOT_FOUND";
    }
    if (is_invalid_camera_descriptor(error)) {
        return "INVALID_CAMERA_DESCRIPTOR";
    }
    if (is_decode_failed(error)) {
        return "DECODE_FAILED";
    }
    if (is_asset_not_found(error)) {
        return "ASSET_NOT_FOUND";
    }
    if (is_frame_dimension_exceeded(error)) {
        return "FRAME_DIMENSION_EXCEEDED";
    }
    if (is_memory_budget_exceeded(error)) {
        return "MEMORY_BUDGET_EXCEEDED";
    }
    if (is_output_open_failed(error)) {
        return "OUTPUT_OPEN_FAILED";
    }
    if (is_video_encoder_failed(error)) {
        return "VIDEO_ENCODER_FAILED";
    }
    if (is_invalid_time_range(error)) {
        return "INVALID_TIME_RANGE";
    }

    switch (error.backend_code) {
        case graph::RenderBackendErrorCode::UnsupportedCapability:
            return "RENDER_UNSUPPORTED_CAPABILITY";
        case graph::RenderBackendErrorCode::InvalidInput:
            return "RENDER_INVALID_INPUT";
        case graph::RenderBackendErrorCode::ExecutionFailure:
            return "RENDER_EXECUTION_FAILURE";
    }
    return "RENDER_INTERNAL_ERROR";
}

[[nodiscard]] inline const char* suggested_fix(
    const graph::NodeExecutionError& error) noexcept
{
    if (is_font_not_found(error)) {
        return "Place the font under the project asset root\n"
               "or change the font path in TextStyle.";
    }
    if (is_unknown_composition(error)) {
        return "Run `chronon list` and use one of the registered composition names.";
    }
    if (is_composition_create_failure(error)) {
        return "Check the composition props and factory inputs, then retry the command.";
    }
    if (is_asset_not_found(error)) {
        return "Place the asset under the project asset root\n"
               "or update the layer asset path.";
    }
    if (is_decode_failed(error)) {
        return "Replace the corrupt or unsupported media file\n"
               "and verify that its decoder is enabled in this build.";
    }
    if (is_invalid_camera_descriptor(error)) {
        return "Correct the camera descriptor fields and projection values\n"
               "before compiling the camera program.";
    }
    if (is_camera_target_not_found(error)) {
        return "Use an existing target layer/node name\n"
               "or remove the missing camera target reference.";
    }
    if (is_frame_dimension_exceeded(error)) {
        return "Reduce the composition or output dimensions\n"
               "to the renderer and encoder limits.";
    }
    if (is_memory_budget_exceeded(error)) {
        return "Reduce resolution, SSAA, motion-blur samples or cache budgets\n"
               "or increase the configured render memory budget.";
    }
    if (is_output_open_failed(error)) {
        return "Choose a writable output path\n"
               "and create its parent directory if required.";
    }
    if (is_video_encoder_failed(error)) {
        return "Use an available codec/encoder and verify FFmpeg configuration\n"
               "before retrying the video render.";
    }
    if (is_invalid_time_range(error)) {
        return "Use a frame range whose start is not greater than its end.";
    }

    switch (error.backend_code) {
        case graph::RenderBackendErrorCode::UnsupportedCapability:
            return "Use a Chronon3D build whose renderer supports the required capability.";
        case graph::RenderBackendErrorCode::InvalidInput:
            return "Check the composition input and required render services.";
        case graph::RenderBackendErrorCode::ExecutionFailure:
            return "Inspect the failing layer and the detailed backend message above.";
    }
    return "Inspect the render diagnostics for the failing frame.";
}

} // namespace render_error_detail

/// Print one canonical, human-actionable CLI representation of the existing
/// graph::NodeExecutionError. This is presentation only: it does not create a
/// second error hierarchy and it does not reinterpret successful renders.
inline void print_render_error(
    const graph::NodeExecutionError& error,
    std::string_view composition_id,
    std::optional<Frame> frame)
{
    const std::string asset = render_error_detail::extract_asset(error.message);

    std::cerr << render_error_detail::stable_cli_code(error) << "\n\n";
    if (!composition_id.empty()) {
        std::cerr << "Composition: " << composition_id << '\n';
    }
    if (render_error_detail::is_user_layer_name(error.node_name)) {
        std::cerr << "Layer: " << error.node_name << '\n';
    }
    if (frame.has_value()) {
        std::cerr << "Frame: " << frame->as_i64() << '\n';
    }
    if (!asset.empty()) {
        std::cerr << "Asset: " << asset << '\n';
    }
    if (!error.message.empty()) {
        std::cerr << "\nDetails:\n" << error.message << '\n';
    }
    std::cerr << "\nFix:\n"
              << render_error_detail::suggested_fix(error)
              << '\n';
}

inline void print_render_error(
    const graph::NodeExecutionError& error,
    std::string_view composition_id,
    Frame frame)
{
    print_render_error(error, composition_id, std::optional<Frame>{frame});
}

inline void print_render_error(
    const graph::NodeExecutionError& error,
    std::string_view composition_id)
{
    print_render_error(error, composition_id, std::nullopt);
}

} // namespace chronon3d::cli
