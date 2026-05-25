#include "render_preflight_helpers.hpp"

#include <sstream>

namespace chronon3d {

const char* severity_label(PreflightSeverity s) {
    switch (s) {
        case PreflightSeverity::Info:    return "INFO";
        case PreflightSeverity::Warning: return "WARNING";
        case PreflightSeverity::Error:   return "ERROR";
    }
    return "UNKNOWN";
}

const char* asset_type_label(PreflightAssetType t) {
    switch (t) {
        case PreflightAssetType::Image:          return "image";
        case PreflightAssetType::Video:          return "video";
        case PreflightAssetType::Font:           return "font";
        case PreflightAssetType::Audio:          return "audio";
        case PreflightAssetType::OutputPath:     return "output_path";
        case PreflightAssetType::Directory:      return "directory";
        case PreflightAssetType::ExternalTool:   return "external_tool";
        case PreflightAssetType::RegisteredAsset: return "registered_asset";
    }
    return "unknown";
}

std::string format_preflight_issues_text(const std::vector<PreflightIssue>& issues) {
    if (issues.empty()) {
        return "";
    }

    std::ostringstream ss;
    ss << "\n================================================================================\n";
    ss << "                     CHRONON3D PREFLIGHT FAILED\n";
    ss << "================================================================================\n\n";

    for (const auto& issue : issues) {
        ss << "[" << severity_label(issue.severity) << "] " << issue.code << "\n";
        if (!issue.path.empty()) {
            ss << "  Path:       " << issue.path << "\n";
        }
        if (!issue.resolved_path.empty() && issue.resolved_path != issue.path) {
            ss << "  Resolved:   " << issue.resolved_path << "\n";
        }
        if (!issue.composition_id.empty()) {
            ss << "  Composition: " << issue.composition_id << "\n";
        }
        if (!issue.layer_id.empty()) {
            ss << "  Layer:      " << issue.layer_id << "\n";
        }
        if (!issue.message.empty()) {
            ss << "  Message:    " << issue.message << "\n";
        }
        if (!issue.recommendation.empty()) {
            ss << "\n  Recommendation:\n    " << issue.recommendation << "\n";
        }
        ss << "\n";
    }

    ss << "================================================================================\n";
    return ss.str();
}

nlohmann::json preflight_issues_to_json(const std::vector<PreflightIssue>& issues) {
    nlohmann::json js;
    js["schema"] = "chronon3d.preflight.v1";

    int error_count   = 0;
    int warning_count = 0;
    int info_count    = 0;

    for (const auto& i : issues) {
        switch (i.severity) {
            case PreflightSeverity::Error:   ++error_count;   break;
            case PreflightSeverity::Warning: ++warning_count; break;
            case PreflightSeverity::Info:    ++info_count;    break;
        }
    }

    js["ok"]       = (error_count == 0);
    js["errors"]   = error_count;
    js["warnings"] = warning_count;
    js["infos"]    = info_count;

    nlohmann::json issues_arr = nlohmann::json::array();
    for (const auto& issue : issues) {
        nlohmann::json item;
        item["severity"]  = severity_label(issue.severity);
        item["code"]      = issue.code;
        item["type"]      = asset_type_label(issue.type);
        item["path"]      = issue.path;
        item["resolved_path"] = issue.resolved_path;
        item["composition_id"] = issue.composition_id;
        item["layer_id"]  = issue.layer_id;
        item["message"]   = issue.message;
        item["recommendation"] = issue.recommendation;
        issues_arr.push_back(item);
    }
    js["issues"] = issues_arr;

    return js;
}

} // namespace chronon3d
