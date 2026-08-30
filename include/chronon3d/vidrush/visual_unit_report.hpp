#pragma once

// ============================================================================
// vidrush/visual_unit_report.hpp
//
// Canonical provenance report for one VidRush visual unit.
//
// This contract deliberately remains separate from render timing telemetry:
// it records the editorial/provider decision and the materialized asset that
// feeds a Chronon RenderPlan. Fields that are not available on a provider
// path are represented by std::optional and serialize as JSON null.
// ============================================================================

#include <chronon3d/core/types/result.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace chronon3d::vidrush {

inline constexpr const char* kVisualUnitReportSchema =
    "vidrush.visual-unit-report.v1";

struct VisualUnitTimeRange {
    std::int64_t start_ms{0};
    std::int64_t end_ms{0};
    std::int64_t target_duration_ms{0};
};

struct SemanticProfileReport {
    std::vector<std::string> entities;
    std::vector<std::string> important_phrases;
    std::vector<std::string> visual_terms;
};

struct ProviderDecisionReport {
    std::string selected;
    std::string reason;
    std::string model;
};

struct CandidateReport {
    std::int64_t candidates_considered{0};
    std::optional<std::string> selected_candidate;
    std::optional<std::int64_t> selected_source_duration_ms;
    std::optional<std::string> source_url;
};

struct SelectedWindowReport {
    std::optional<std::int64_t> start_ms;
    std::optional<std::int64_t> end_ms;
    std::optional<std::int64_t> duration_ms;
};

struct AssetReport {
    std::optional<std::string> asset_id;
    std::optional<std::string> sha256;
    std::optional<std::string> local_path;
};

struct DriveReport {
    std::optional<std::string> destination;
    std::optional<std::string> link;
    std::optional<std::string> file_id;
};

enum class CacheStatus : std::uint8_t {
    Unknown,
    Hit,
    Miss,
    Invalidated,
    Rebuilt,
};

struct CacheReport {
    CacheStatus status{CacheStatus::Unknown};
    std::optional<std::string> key;
    std::int64_t discovery_calls{0};
    std::int64_t research_calls{0};
    std::int64_t provider_calls{0};
    std::int64_t download_calls{0};
    std::int64_t transcode_calls{0};
    std::int64_t drive_upload_calls{0};
};

struct VisualUnitReport {
    std::string scene_id;
    std::string visual_unit_id;
    VisualUnitTimeRange time;
    SemanticProfileReport semantic_profile;
    ProviderDecisionReport provider_decision;
    std::vector<std::string> query_plan;
    CandidateReport candidate;
    SelectedWindowReport selected_window;
    AssetReport asset;
    DriveReport drive;
    CacheReport cache;
};

struct VisualUnitReportDocument {
    std::string schema{kVisualUnitReportSchema};
    std::string job_id;
    std::vector<VisualUnitReport> visual_units;
};

struct VisualUnitReportError {
    std::string path;
    std::string message;
};

/// Validate one visual-unit record. Required identity, timing, provider and
/// cache counters fail loudly; provider-specific optional facts may be null.
[[nodiscard]] Result<bool, VisualUnitReportError> validate_visual_unit_report(
    const VisualUnitReport& report);

/// Validate and serialize a report document. Throws no exceptions for
/// validation failures; callers receive a structured error instead.
[[nodiscard]] Result<nlohmann::json, VisualUnitReportError>
visual_unit_report_to_json(const VisualUnitReportDocument& document);

/// Decode a previously serialized report document. Unknown future fields are
/// ignored so consumers can read compatible additive schema revisions.
[[nodiscard]] Result<VisualUnitReportDocument, VisualUnitReportError>
visual_unit_report_from_json(const nlohmann::json& root);

[[nodiscard]] const char* cache_status_name(CacheStatus status) noexcept;
[[nodiscard]] std::optional<CacheStatus> parse_cache_status(std::string_view value) noexcept;

}  // namespace chronon3d::vidrush
