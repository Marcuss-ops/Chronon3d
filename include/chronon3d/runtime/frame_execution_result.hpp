#pragma once

#include <cstdint>
#include <chronon3d/core/error.hpp>
#include <limits>
#include <string>
#include <utility>

namespace chronon3d::runtime {

using PassId = std::uint32_t;
inline constexpr PassId kInvalidPassId = std::numeric_limits<PassId>::max();

enum class ExecutionFailure : std::uint8_t {
    None,
    DecodeFailure,
    InvalidResource,
    OutOfMemory,
    DeviceLost,
    EncodeFailure,
    MuxFailure,
    // A RenderBackend operation failed at runtime (shaping, raster, surface
    // or device call).  Distinct from InvalidResource so an upstream caller
    // can tell "malformed input" from "real execution fault"; appended last
    // to keep existing enumerator values stable.
    BackendFailure,
};

[[nodiscard]] inline const char* execution_failure_name(
    ExecutionFailure failure) noexcept {
    switch (failure) {
        case ExecutionFailure::None:            return "None";
        case ExecutionFailure::DecodeFailure:   return "DecodeFailure";
        case ExecutionFailure::InvalidResource: return "InvalidResource";
        case ExecutionFailure::OutOfMemory:     return "OutOfMemory";
        case ExecutionFailure::DeviceLost:      return "DeviceLost";
        case ExecutionFailure::EncodeFailure:   return "EncodeFailure";
        case ExecutionFailure::MuxFailure:      return "MuxFailure";
        case ExecutionFailure::BackendFailure:  return "BackendFailure";
    }
    return "Unknown";
}

/// Canonical frame/job failure boundary. It is intentionally a value type: no
/// recovery manager, global service, or hidden fallback is attached to it.
/// Callers decide retry/recreate policy after receiving the explicit failure.
struct FrameExecutionResult {
    ExecutionFailure failure{ExecutionFailure::None};
    PassId failing_pass{kInvalidPassId};
    std::string reason{};
    chronon::Error error{};

    [[nodiscard]] bool ok() const noexcept {
        return failure == ExecutionFailure::None;
    }

    [[nodiscard]] static FrameExecutionResult succeeded() {
        return {};
    }

    [[nodiscard]] static FrameExecutionResult failed(
        ExecutionFailure failure_code,
        std::string failure_reason,
        PassId pass = kInvalidPassId) {
        const auto kind = failure_code == ExecutionFailure::DeviceLost
            ? chronon::ErrorKind::DeviceLost
            : failure_code == ExecutionFailure::InvalidResource
                ? chronon::ErrorKind::InvalidPlan
                : failure_code == ExecutionFailure::EncodeFailure ||
                    failure_code == ExecutionFailure::MuxFailure ||
                    failure_code == ExecutionFailure::OutOfMemory
                    ? chronon::ErrorKind::JobFailed
                    : chronon::ErrorKind::FrameFailed;
        return FrameExecutionResult{
            .failure = failure_code,
            .failing_pass = pass,
            .reason = failure_reason,
            .error = chronon::from_legacy<ExecutionFailure>(
                kind, "frame_execution", "chronon3d::runtime::ExecutionFailure",
                execution_failure_name(failure_code), std::move(failure_reason)),
        };
    }

    [[nodiscard]] static FrameExecutionResult failed(
        chronon::Error canonical_error,
        PassId pass = kInvalidPassId) {
        return FrameExecutionResult{
            .failure = ExecutionFailure::BackendFailure,
            .failing_pass = pass,
            .reason = canonical_error.detail.message,
            .error = std::move(canonical_error),
        };
    }

    [[nodiscard]] static FrameExecutionResult cancelled(
        std::string reason,
        PassId pass = kInvalidPassId) {
        return failed(chronon::from_legacy<ExecutionFailure>(
            chronon::ErrorKind::Cancelled, "frame_execution",
            "chronon3d::runtime::ExecutionFailure", "Cancelled",
            std::move(reason)), pass);
    }
};

} // namespace chronon3d::runtime
