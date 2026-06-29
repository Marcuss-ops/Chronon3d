#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// include/chronon3d/sdk/render_error.hpp
//
// P1-A — Skeleton SDK error type.
// ═════════════════════════════════════════════════════════════════════════════
//
// `RenderErrorCode` enumerates the failure paths surfaced through the
// public SDK surface.  `RenderError` aggregates the code with a free-
// form diagnostic message.
//
// Discriminant order is the stable ABI contract: do NOT reorder
// existing entries (add new ones at the tail before
// `InternalError`).
// ═════════════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <string>

namespace chronon3d::sdk {

/// Categorical failure code reported by `sdk::RenderEngine::render()`.
enum class RenderErrorCode : std::uint8_t {
    Ok                 = 0,
    InvalidComposition = 1,   ///< Composition identity invalid or unfetchable
    InvalidSettings    = 2,   ///< RenderSettings out of range / unsupported
    BackendUnavailable = 3,   ///< No registered backend can handle the request
    CompileFailure     = 4,   ///< RenderGraph / scene-program compile failed
    RuntimeFailure     = 5,   ///< Backend-side render exception
    Cancelled          = 6,   ///< CancellationToken fired during render
    BudgetExceeded     = 7,   ///< CPU / memory / time budget exhausted
    InternalError      = 255, ///< Catch-all OPP-side error
};

/// Structured error payload returned by `sdk::RenderEngine::render()`.
struct RenderError {
    RenderErrorCode code{RenderErrorCode::Ok};
    std::string     message;   ///< Free-form diagnostic, never null
};

/// Stable string-form name for each `RenderErrorCode`.  For logging
/// and diagnostics only; never use for equality — enum storage may
/// evolve, order is the contract.
inline const char* render_error_code_name(RenderErrorCode c) noexcept {
    switch (c) {
        case RenderErrorCode::Ok:                 return "Ok";
        case RenderErrorCode::InvalidComposition: return "InvalidComposition";
        case RenderErrorCode::InvalidSettings:    return "InvalidSettings";
        case RenderErrorCode::BackendUnavailable: return "BackendUnavailable";
        case RenderErrorCode::CompileFailure:     return "CompileFailure";
        case RenderErrorCode::RuntimeFailure:     return "RuntimeFailure";
        case RenderErrorCode::Cancelled:          return "Cancelled";
        case RenderErrorCode::BudgetExceeded:     return "BudgetExceeded";
        case RenderErrorCode::InternalError:      return "InternalError";
    }
    return "Unknown";
}

} // namespace chronon3d::sdk
