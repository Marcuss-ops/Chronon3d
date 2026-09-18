#pragma once

// Canonical error model. Domain-specific enums remain source-compatible while
// migrating; their names and values are carried by ErrorDetail and never form
// a second engine-level control-flow taxonomy.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace chronon {

enum class ErrorCategory : std::uint8_t {
    Recoverable,
    FrameFailed,
    JobFailed,
    Cancelled,
    DeviceLost,
    InvalidPlan,
    InvariantViolation,
};

// Compatibility spelling for code already migrated to the first Phase 1
// draft. There is still only one enum and one canonical payload.
using ErrorKind = ErrorCategory;

struct ErrorDetail {
    std::string domain;
    std::string legacy_type;
    std::string legacy_code;
    std::string message;
    std::string component;
    std::string node_id;
    std::string asset;
    std::int64_t frame{-1};
    std::uint64_t job_id{0};
};

struct Error {
    ErrorCategory category{ErrorCategory::InvariantViolation};
    ErrorDetail detail{};

    [[nodiscard]] bool is_cancelled() const noexcept {
        return category == ErrorCategory::Cancelled;
    }
    [[nodiscard]] bool is_terminal() const noexcept {
        return category == ErrorCategory::JobFailed ||
               category == ErrorCategory::DeviceLost ||
               category == ErrorCategory::InvalidPlan ||
               category == ErrorCategory::InvariantViolation;
    }
};

struct LegacyErrorMapping {
    std::string_view legacy_type;
    std::string_view domain;
};

// Every legacy error enum has a recorded migration slot. Removing an entry
// requires removing the enum or moving all direct construction to an adapter.
// Keep this list exact: stale or duplicate rows make the migration gate lie.
inline constexpr std::array<LegacyErrorMapping, 27> kLegacyErrorMappings{{
    {"chronon3d::sdk::RenderErrorCode", "sdk"},
    {"chronon3d::render::RenderDiagnosticCode", "render_diagnostic"},
    {"chronon3d::graph::RenderBackendErrorCode", "render_backend"},
    {"chronon3d::graph::BackendResolveErrorCode", "backend_registry"},
    {"chronon3d::compiler::CompileErrorCode", "compiler"},
    {"chronon3d::RenderJobErrorCode", "render_job"},
    {"chronon3d::runtime::job::RenderJobRequestErrorCode", "job_request"},
    {"chronon3d::graph::ScopeErrorCode", "execution_scope"},
    {"chronon3d::TextErrorCode", "text"},
    {"chronon3d::TextLayoutErrorKind", "text_layout"},
    {"chronon3d::CameraErrorCode", "camera"},
    {"chronon3d::CameraCompileErrorCode", "camera_compile"},
    {"chronon3d::media::video::MediaProbeErrorCode", "media_probe"},
    {"chronon3d::media::video::VideoSinkError", "video_sink"},
    {"chronon3d::media::ConversionError", "frame_conversion"},
    {"chronon3d::trace::TraceError", "trace"},
    {"chronon3d::runtime::ExecutionFailure", "frame_execution"},
    {"chronon3d::media::DecodeFailureReason", "decode"},
    {"chronon3d::PropsErrorReason", "composition_props"},
    {"chronon3d::render_plan::ValidationIssueKind", "render_plan"},
    {"chronon3d::runtime::RuntimeBuildError::Code", "runtime"},
    {"chronon3d::runtime::PreparationError::Code", "preparation"},
    {"chronon3d::scene::CameraFailurePolicy", "camera_policy"},
    {"chronon3d::graph::FrameExecutionPath", "frame_execution_path"},
    {"chronon3d::cache::CacheRemovalReason", "cache"},
    {"chronon3d::runtime::AsyncEncoderSinkState", "encoder_sink"},
    {"chronon3d::runtime::GpuMemoryPressure", "gpu_memory"},
}};

[[nodiscard]] inline bool is_registered_legacy_type(
    std::string_view legacy_type) noexcept {
    for (const auto& mapping : kLegacyErrorMappings) {
        if (mapping.legacy_type == legacy_type) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline const char* error_category_name(ErrorCategory category) noexcept {
    switch (category) {
        case ErrorCategory::Recoverable:         return "Recoverable";
        case ErrorCategory::FrameFailed:         return "FrameFailed";
        case ErrorCategory::JobFailed:           return "JobFailed";
        case ErrorCategory::Cancelled:           return "Cancelled";
        case ErrorCategory::DeviceLost:          return "DeviceLost";
        case ErrorCategory::InvalidPlan:         return "InvalidPlan";
        case ErrorCategory::InvariantViolation: return "InvariantViolation";
    }
    return "Unknown";
}

[[nodiscard]] inline const char* error_kind_name(ErrorCategory category) noexcept {
    return error_category_name(category);
}

template <typename LegacyEnum>
[[nodiscard]] Error from_legacy(
    ErrorCategory category,
    std::string_view domain,
    std::string_view legacy_type,
    std::string_view legacy_code,
    std::string message = {},
    std::string component = {},
    std::string node_id = {},
    std::string asset = {},
    std::int64_t frame = -1,
    std::uint64_t job_id = 0) {
    static_assert(std::is_enum_v<LegacyEnum>,
                  "chronon::from_legacy requires an enum detail payload");
    (void)sizeof(LegacyEnum);
    if (!is_registered_legacy_type(legacy_type)) {
        return Error{
            .category = ErrorCategory::InvariantViolation,
            .detail = ErrorDetail{
                .domain = "canonical_error",
                .legacy_type = std::string(legacy_type),
                .legacy_code = "UnregisteredLegacyErrorType",
                .message = "legacy error type is absent from the canonical mapping inventory",
                .component = "from_legacy",
                .frame = frame,
                .job_id = job_id,
            },
        };
    }
    return Error{
        .category = category,
        .detail = ErrorDetail{
            .domain = std::string(domain),
            .legacy_type = std::string(legacy_type),
            .legacy_code = std::string(legacy_code),
            .message = std::move(message),
            .component = std::move(component),
            .node_id = std::move(node_id),
            .asset = std::move(asset),
            .frame = frame,
            .job_id = job_id,
        },
    };
}

[[nodiscard]] inline Error make_error(
    ErrorCategory category,
    std::string domain,
    std::string code,
    std::string message,
    std::string component = {},
    std::string node_id = {},
    std::string asset = {},
    std::int64_t frame = -1,
    std::uint64_t job_id = 0) {
    return Error{
        .category = category,
        .detail = ErrorDetail{
            .domain = std::move(domain),
            .legacy_code = std::move(code),
            .message = std::move(message),
            .component = std::move(component),
            .node_id = std::move(node_id),
            .asset = std::move(asset),
            .frame = frame,
            .job_id = job_id,
        },
    };
}

} // namespace chronon

namespace chronon3d {
using Error = ::chronon::Error;
using ErrorCategory = ::chronon::ErrorCategory;
using ErrorDetail = ::chronon::ErrorDetail;
using ErrorKind = ::chronon::ErrorKind;
using ::chronon::error_category_name;
using ::chronon::error_kind_name;
using ::chronon::from_legacy;
using ::chronon::make_error;
} // namespace chronon3d
