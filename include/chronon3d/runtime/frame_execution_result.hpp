#pragma once

#include <cstdint>
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

/// Canonical frame/job failure boundary. It is intentionally a value type: no
/// recovery manager, global service, or hidden fallback is attached to it.
/// Callers decide retry/recreate policy after receiving the explicit failure.
struct FrameExecutionResult {
    ExecutionFailure failure{ExecutionFailure::None};
    PassId failing_pass{kInvalidPassId};
    std::string reason{};

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
        return FrameExecutionResult{
            .failure = failure_code,
            .failing_pass = pass,
            .reason = std::move(failure_reason),
        };
    }
};

} // namespace chronon3d::runtime
