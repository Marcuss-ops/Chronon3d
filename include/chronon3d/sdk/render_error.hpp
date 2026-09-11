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
#include <chronon3d/core/error.hpp>
#include <string>
#include <utility>

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
    AssetChanged       = 8,   ///< Prepared asset identity changed at render time
    InvalidPlan        = 9,   ///< RenderPlan failed schema or semantic validation
    UnsupportedSchema  = 10,  ///< RenderPlan schema/version is not supported
    AssetNotFound      = 11,  ///< Referenced asset is unavailable
    DecodeFailure      = 12,  ///< Input/media decoding failed
    EncodeFailure      = 13,  ///< Output encoding failed
    OutOfMemory        = 14,  ///< Resource or allocation budget exhausted
    AbiMismatch        = 15,  ///< Public ABI contract version mismatch
    InternalError      = 255, ///< Catch-all OPP-side error
};

/// Structured error payload returned by `sdk::RenderEngine::render()`.
struct RenderError {
    RenderErrorCode code{RenderErrorCode::Ok};
    std::string     message;   ///< Free-form diagnostic, never null
    std::string     component; ///< Stable subsystem name, when known
    std::string     node_id;   ///< Optional logical node identifier
    std::string     asset;     ///< Optional logical asset reference

    /// Phase-1 compatibility bridge. The public legacy code remains ABI/API
    /// compatible, while callers that need control-flow semantics consume the
    /// canonical chronon::Error.
    [[nodiscard]] chronon::Error canonical_error() const;
};

// SDK compatibility boundary. RenderErrorCode remains the public ABI detail;
// all new SDK error creation goes through this adapter so the legacy payload
// cannot acquire competing construction conventions.
[[nodiscard]] inline RenderError make_render_error(
    RenderErrorCode code,
    std::string message,
    std::string component = {},
    std::string node_id = {},
    std::string asset = {}) {
    return RenderError{
        code, std::move(message), std::move(component),
        std::move(node_id), std::move(asset)};
}

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
        case RenderErrorCode::AssetChanged:       return "AssetChanged";
        case RenderErrorCode::InvalidPlan:        return "InvalidPlan";
        case RenderErrorCode::UnsupportedSchema:  return "UnsupportedSchema";
        case RenderErrorCode::AssetNotFound:      return "AssetNotFound";
        case RenderErrorCode::DecodeFailure:      return "DecodeFailure";
        case RenderErrorCode::EncodeFailure:      return "EncodeFailure";
        case RenderErrorCode::OutOfMemory:        return "OutOfMemory";
        case RenderErrorCode::AbiMismatch:        return "AbiMismatch";
        case RenderErrorCode::InternalError:      return "InternalError";
    }
    return "Unknown";
}

inline chronon::Error RenderError::canonical_error() const {
    chronon::ErrorKind kind = chronon::ErrorKind::JobFailed;
    switch (code) {
        case RenderErrorCode::Ok:
            kind = chronon::ErrorKind::Recoverable;
            break;
        case RenderErrorCode::Cancelled:
            kind = chronon::ErrorKind::Cancelled;
            break;
        case RenderErrorCode::InvalidComposition:
        case RenderErrorCode::InvalidSettings:
        case RenderErrorCode::InvalidPlan:
        case RenderErrorCode::UnsupportedSchema:
            kind = chronon::ErrorKind::InvalidPlan;
            break;
        case RenderErrorCode::BackendUnavailable:
            kind = chronon::ErrorKind::Recoverable;
            break;
        case RenderErrorCode::RuntimeFailure:
        case RenderErrorCode::CompileFailure:
        case RenderErrorCode::BudgetExceeded:
        case RenderErrorCode::AssetChanged:
        case RenderErrorCode::AssetNotFound:
        case RenderErrorCode::DecodeFailure:
        case RenderErrorCode::EncodeFailure:
        case RenderErrorCode::OutOfMemory:
            kind = chronon::ErrorKind::FrameFailed;
            break;
        case RenderErrorCode::AbiMismatch:
        case RenderErrorCode::InternalError:
            kind = chronon::ErrorKind::InvariantViolation;
            break;
    }
    return chronon::from_legacy<RenderErrorCode>(
        kind, "sdk", "chronon3d::sdk::RenderErrorCode",
        render_error_code_name(code), message, component, node_id, asset);
}

} // namespace chronon3d::sdk
