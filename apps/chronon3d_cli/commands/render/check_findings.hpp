#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// check_findings.hpp — pure report model for `chronon3d_cli check`.
//
// The model is deliberately free of renderer/media dependencies so the
// severity mapping, threshold defaults, JSON schema and exit-code contract can
// be unit-tested without a GPU, a decoder or a rendered artifact.
// ─────────────────────────────────────────────────────────────────────────────

#include <nlohmann/json.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::cli::check {

enum class Severity : int {
    P0 = 0,
    P1 = 1,
    P2 = 2,
    P3 = 3,
};

[[nodiscard]] bool severity_at_least(Severity value, Severity floor) noexcept;
[[nodiscard]] std::string_view severity_name(Severity value) noexcept;
[[nodiscard]] std::optional<Severity> parse_severity(std::string_view text) noexcept;

struct Thresholds {
    double black_seconds{0.5};
    double freeze_seconds{1.0};
    double freeze_max_luma_delta{0.0005};
    double flash_luma_delta{0.15};
    int flash_max_per_second{3};
    double max_clipped_fraction{0.02};
    double min_font_px{20.0};
    double min_font_reference_height{1080.0};
    double loudness_target_lufs{-14.0};
    double loudness_tolerance_lu{2.0};
    double max_peak_dbfs{-1.0};
    double max_audio_clipped_fraction{0.0001};
    double silence_lufs{-60.0};
    Severity fail_on{Severity::P1};
};

struct Finding {
    std::string id;
    std::string category;
    Severity severity{Severity::P2};
    std::string message;
    std::string fix;
    std::optional<int> first_frame;
    std::optional<int> last_frame;
    std::optional<double> first_second;
    std::optional<double> last_second;
    nlohmann::json details{nlohmann::json::object()};
};

struct Report {
    std::string input;
    std::string plan;
    Thresholds thresholds;
    std::vector<Finding> findings;
    std::vector<std::string> checks_run;
    std::vector<std::string> checks_not_measured;
    std::vector<std::string> notes;
};

void add_finding(Report& report, Finding finding);
void add_check_run(Report& report, std::string name);
void add_check_not_measured(Report& report, std::string reason);

[[nodiscard]] std::array<int, 4> severity_counts(const Report& report) noexcept;
[[nodiscard]] bool has_failure(const Report& report) noexcept;
[[nodiscard]] int exit_code(const Report& report) noexcept;

[[nodiscard]] nlohmann::json report_to_json(const Report& report);
[[nodiscard]] std::string report_to_text(const Report& report);

} // namespace chronon3d::cli::check
