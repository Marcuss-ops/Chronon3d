#pragma once

#include "render_error_formatter.hpp"

#include <chronon3d/timeline/render_job.hpp>

#include <optional>

namespace chronon3d::cli {

[[nodiscard]] inline graph::RenderBackendErrorCode render_backend_code_for(
    RenderJobErrorCode code) noexcept
{
    switch (code) {
        case RenderJobErrorCode::UnsupportedMode:
            return graph::RenderBackendErrorCode::UnsupportedCapability;
        case RenderJobErrorCode::InvalidJob:
        case RenderJobErrorCode::ValidationFailed:
            return graph::RenderBackendErrorCode::InvalidInput;
        case RenderJobErrorCode::SetupFailed:
        case RenderJobErrorCode::RenderFailed:
            return graph::RenderBackendErrorCode::ExecutionFailure;
    }
    return graph::RenderBackendErrorCode::ExecutionFailure;
}

/// Adapter overload for the existing RenderJobError value. Presentation still
/// flows through the single NodeExecutionError formatter above; this introduces
/// no second error hierarchy or stable-code registry.
inline void print_render_error(
    const RenderJobError& error,
    const RenderJob& job)
{
    const graph::NodeExecutionError node_error{
        render_backend_code_for(error.code),
        "render",
        error.message,
    };

    std::optional<Frame> frame;
    if (!render_error_detail::is_invalid_time_range(node_error)) {
        switch (job.mode) {
            case RenderMode::Still:
                frame = job.still_frame;
                break;
            case RenderMode::Sequence:
            case RenderMode::Video:
                frame = job.first_frame;
                break;
        }
    }

    print_render_error(node_error, job.comp_id, frame);
}

} // namespace chronon3d::cli
