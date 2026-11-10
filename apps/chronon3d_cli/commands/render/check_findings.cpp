#include "check_findings.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace chronon3d::cli::check {

bool severity_at_least(const Severity value, const Severity floor) noexcept {
    return static_cast<int>(value) <= static_cast<int>(floor);
}

std::string_view severity_name(const Severity value) noexcept {
    switch (value) {
        case Severity::P0: return "P0";
        case Severity::P1: return "P1";
        case Severity::P2: return "P2";
        case Severity::P3: return "P3";
    }
    return "P3";
}

std::optional<Severity> parse_severity(const std::string_view text) noexcept {
    if (text == "P0" || text == "p0") return Severity::P0;
    if (text == "P1" || text == "p1") return Severity::P1;
    if (text == "P2" || text == "p2") return Severity::P2;
    if (text == "P3" || text == "p3") return Severity::P3;
    return std::nullopt;
}

void add_finding(Report& report, Finding finding) {
    report.findings.push_back(std::move(finding));
}

void add_check_run(Report& report, std::string name) {
    if (std::find(report.checks_run.begin(), report.checks_run.end(), name) ==
        report.checks_run.end()) {
        report.checks_run.push_back(std::move(name));
    }
}

void add_check_not_measured(Report& report, std::string reason) {
    if (std::find(report.checks_not_measured.begin(),
                  report.checks_not_measured.end(), reason) ==
        report.checks_not_measured.end()) {
        report.checks_not_measured.push_back(std::move(reason));
    }
}

std::array<int, 4> severity_counts(const Report& report) noexcept {
    std::array<int, 4> counts{0, 0, 0, 0};
    for (const auto& finding : report.findings) {
        ++counts[static_cast<std::size_t>(finding.severity)];
    }
    return counts;
}

bool has_failure(const Report& report) noexcept {
    for (const auto& finding : report.findings) {
        if (severity_at_least(finding.severity, report.thresholds.fail_on)) {
            return true;
        }
    }
    return false;
}

int exit_code(const Report& report) noexcept {
    return has_failure(report) ? 1 : 0;
}

nlohmann::json report_to_json(const Report& report) {
    const auto counts = severity_counts(report);
    nlohmann::json findings = nlohmann::json::array();
    for (const auto& finding : report.findings) {
        nlohmann::json item = {
            {"id", finding.id},
            {"category", finding.category},
            {"severity", severity_name(finding.severity)},
            {"message", finding.message},
            {"fix", finding.fix},
            {"details", finding.details},
        };
        if (finding.first_frame) item["first_frame"] = *finding.first_frame;
        if (finding.last_frame) item["last_frame"] = *finding.last_frame;
        if (finding.first_second) item["first_second"] = *finding.first_second;
        if (finding.last_second) item["last_second"] = *finding.last_second;
        findings.push_back(std::move(item));
    }

    return {
        {"schema", "chronon.check.v1"},
        {"status", has_failure(report) ? "FAIL" : "PASS"},
        {"input", report.input},
        {"plan", report.plan},
        {"thresholds", {
            {"black_seconds", report.thresholds.black_seconds},
            {"freeze_seconds", report.thresholds.freeze_seconds},
            {"freeze_max_luma_delta", report.thresholds.freeze_max_luma_delta},
            {"flash_luma_delta", report.thresholds.flash_luma_delta},
            {"flash_max_per_second", report.thresholds.flash_max_per_second},
            {"max_clipped_fraction", report.thresholds.max_clipped_fraction},
            {"min_font_px", report.thresholds.min_font_px},
            {"min_font_reference_height", report.thresholds.min_font_reference_height},
            {"loudness_target_lufs", report.thresholds.loudness_target_lufs},
            {"loudness_tolerance_lu", report.thresholds.loudness_tolerance_lu},
            {"max_peak_dbfs", report.thresholds.max_peak_dbfs},
            {"max_audio_clipped_fraction", report.thresholds.max_audio_clipped_fraction},
            {"silence_lufs", report.thresholds.silence_lufs},
            {"fail_on", severity_name(report.thresholds.fail_on)},
        }},
        {"findings", std::move(findings)},
        {"summary", {
            {"fail_on", severity_name(report.thresholds.fail_on)},
            {"counts", {
                {"P0", counts[0]},
                {"P1", counts[1]},
                {"P2", counts[2]},
                {"P3", counts[3]},
            }},
            {"total", static_cast<int>(report.findings.size())},
            {"checks_run", report.checks_run},
            {"checks_not_measured", report.checks_not_measured},
            {"notes", report.notes},
        }},
    };
}

std::string report_to_text(const Report& report) {
    const auto counts = severity_counts(report);
    std::ostringstream out;
    out << "check " << report.input;
    if (!report.plan.empty()) out << "  plan=" << report.plan;
    out << '\n'
        << "status " << (has_failure(report) ? "FAIL" : "PASS")
        << "  fail_on " << severity_name(report.thresholds.fail_on)
        << "  P0=" << counts[0] << " P1=" << counts[1]
        << " P2=" << counts[2] << " P3=" << counts[3] << '\n';

    if (report.findings.empty()) {
        out << "no findings\n";
    }
    for (const auto& finding : report.findings) {
        out << severity_name(finding.severity) << "  " << finding.id
            << "  " << finding.message;
        if (finding.first_frame) {
            out << "  frames " << *finding.first_frame;
            if (finding.last_frame && *finding.last_frame != *finding.first_frame) {
                out << '-' << *finding.last_frame;
            }
        }
        out << '\n';
        if (!finding.fix.empty()) out << "    fix: " << finding.fix << '\n';
    }

    if (!report.checks_run.empty()) {
        out << "checks: ";
        for (std::size_t i = 0; i < report.checks_run.size(); ++i) {
            if (i != 0) out << ", ";
            out << report.checks_run[i];
        }
        out << '\n';
    }
    if (!report.checks_not_measured.empty()) {
        out << "not measured: ";
        for (std::size_t i = 0; i < report.checks_not_measured.size(); ++i) {
            if (i != 0) out << "; ";
            out << report.checks_not_measured[i];
        }
        out << '\n';
    }
    for (const auto& note : report.notes) out << "note: " << note << '\n';
    return out.str();
}

} // namespace chronon3d::cli::check
