#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

#include <iostream>
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

[[nodiscard]] inline std::string extract_font_asset(std::string_view message) {
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

    return {};
}

[[nodiscard]] inline bool is_font_not_found(
    const graph::NodeExecutionError& error) noexcept
{
    return contains(error.message, "failed to load font face") ||
           contains(error.message, "empty layout.font.font_path");
}

[[nodiscard]] inline const char* stable_cli_code(
    const graph::NodeExecutionError& error) noexcept
{
    if (is_font_not_found(error)) {
        return "TEXT_FONT_NOT_FOUND";
    }
    if (contains(error.message, "unknown composition")) {
        return "UNKNOWN_COMPOSITION";
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
    Frame frame)
{
    const std::string asset = render_error_detail::extract_font_asset(error.message);

    std::cerr << render_error_detail::stable_cli_code(error) << "\n\n";
    if (!composition_id.empty()) {
        std::cerr << "Composition: " << composition_id << '\n';
    }
    if (!error.node_name.empty()) {
        std::cerr << "Layer: " << error.node_name << '\n';
    }
    std::cerr << "Frame: " << frame.as_i64() << '\n';
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

} // namespace chronon3d::cli
