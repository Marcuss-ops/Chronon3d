#include <chronon3d/assets/render_preflight.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace chronon3d {

// ---------------------------------------------------------------------------
// Tool existence check
// ---------------------------------------------------------------------------

bool tool_exists_in_path(const std::string& tool) {
#if defined(_WIN32)
    std::string cmd = "where " + tool + " >nul 2>nul";
#else
    std::string cmd = "command -v " + tool + " >/dev/null 2>&1";
#endif
    return std::system(cmd.c_str()) == 0;
}

// ---------------------------------------------------------------------------
// Requirement registration
// ---------------------------------------------------------------------------

void RenderPreflight::require_image(const std::string& path) {
    PreflightRequirement req;
    req.type = PreflightAssetType::Image;
    req.path = path;
    m_requirements.push_back(req);
}

void RenderPreflight::require_video(const std::string& path) {
    PreflightRequirement req;
    req.type = PreflightAssetType::Video;
    req.path = path;
    m_requirements.push_back(req);
}

void RenderPreflight::require_font(const std::string& path) {
    PreflightRequirement req;
    req.type = PreflightAssetType::Font;
    req.path = path;
    m_requirements.push_back(req);
}

void RenderPreflight::require_audio(const std::string& path) {
    PreflightRequirement req;
    req.type = PreflightAssetType::Audio;
    req.path = path;
    m_requirements.push_back(req);
}

void RenderPreflight::require_output_path(const std::string& path) {
    PreflightRequirement req;
    req.type = PreflightAssetType::OutputPath;
    req.path = path;
    m_requirements.push_back(req);
}

void RenderPreflight::require_external_tool(const std::string& name) {
    PreflightRequirement req;
    req.type = PreflightAssetType::ExternalTool;
    req.path = name;
    m_requirements.push_back(req);
}

void RenderPreflight::add_requirements(const std::vector<PreflightRequirement>& reqs) {
    m_requirements.insert(m_requirements.end(), reqs.begin(), reqs.end());
}

// ---------------------------------------------------------------------------
// Validation helpers
// ---------------------------------------------------------------------------

namespace {

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

const char* severity_label(PreflightSeverity s) {
    switch (s) {
        case PreflightSeverity::Info:    return "INFO";
        case PreflightSeverity::Warning: return "WARNING";
        case PreflightSeverity::Error:   return "ERROR";
    }
    return "UNKNOWN";
}

void validate_file_exists(const PreflightRequirement& req,
                          const std::string& code,
                          PreflightAssetType type,
                          std::vector<PreflightIssue>& issues) {
    namespace fs = std::filesystem;
    std::string resolved = AssetRegistry::resolve(req.path);

    if (!fs::exists(resolved)) {
        PreflightIssue issue;
        issue.severity       = PreflightSeverity::Error;
        issue.type           = type;
        issue.code           = code;
        issue.path           = req.path;
        issue.resolved_path  = resolved;
        issue.composition_id = req.composition_id;
        issue.layer_id       = req.layer_id;
        issue.message        = std::string("Missing ") + asset_type_label(type) + ": '" + req.path + "'";
        issue.recommendation = "Make sure the file exists, or mount the correct assets directory with AssetRegistry::mount().";
        issues.push_back(issue);
    }
}

void validate_output_writable(const PreflightRequirement& req,
                               std::vector<PreflightIssue>& issues) {
    namespace fs = std::filesystem;
    fs::path output_path(req.path);
    fs::path parent = output_path.parent_path();

    if (parent.empty()) {
        parent = ".";
    }

    // Check if parent directory exists
    if (!fs::exists(parent)) {
        // Walk up to find the first existing ancestor to check writability
        fs::path check_parent = parent;
        while (!check_parent.empty() && !fs::exists(check_parent)) {
            check_parent = check_parent.parent_path();
        }

        // If we found a writable ancestor, the directory could be created.
        // If not, or if we walked all the way up, report a warning (not error —
        // the render pipeline will attempt creation at render time).
        bool can_create = !check_parent.empty() && fs::exists(check_parent);
        if (can_create) {
            // Check if the nearest existing ancestor is writable
            auto tmp = check_parent / ".chronon3d_preflight_test";
            std::ofstream test(tmp);
            if (test.is_open()) {
                test.close();
                std::error_code ec;
                fs::remove(tmp, ec);
                // Ancestor is writable, so directory can be created at render time
                // No error to report
                return;
            }
        }

        // Can't verify writability of a non-existent path without creating it.
        // Report a warning — the render pipeline may succeed or fail.
        PreflightIssue issue;
        issue.severity       = PreflightSeverity::Warning;
        issue.type           = PreflightAssetType::OutputPath;
        issue.code           = "OUTPUT_DIR_MISSING";
        issue.path           = req.path;
        issue.composition_id = req.composition_id;
        issue.layer_id       = req.layer_id;
        issue.message        = "Output directory does not exist: '" + parent.string() + "'";
        issue.recommendation = "The render pipeline will attempt to create it. Ensure the parent path is writable.";
        issues.push_back(issue);
        return;
    }

    // Directory exists — check writability
    auto tmp = parent / ".chronon3d_preflight_test";
    {
        std::ofstream test(tmp);
        if (!test.is_open()) {
            PreflightIssue issue;
            issue.severity       = PreflightSeverity::Error;
            issue.type           = PreflightAssetType::OutputPath;
            issue.code           = "OUTPUT_NOT_WRITABLE";
            issue.path           = req.path;
            issue.composition_id = req.composition_id;
            issue.layer_id       = req.layer_id;
            issue.message        = "Output directory is not writable: '" + parent.string() + "'";
            issue.recommendation = "Check filesystem permissions.";
            issues.push_back(issue);
            return;
        }
    }
    std::error_code ec;
    fs::remove(tmp, ec);

    // Warn if output file already exists
    if (fs::exists(output_path)) {
        PreflightIssue issue;
        issue.severity       = PreflightSeverity::Warning;
        issue.type           = PreflightAssetType::OutputPath;
        issue.code           = "OUTPUT_EXISTS";
        issue.path           = req.path;
        issue.composition_id = req.composition_id;
        issue.layer_id       = req.layer_id;
        issue.message        = "Output file already exists: '" + req.path + "'";
        issue.recommendation = "Use --overwrite to replace the existing file.";
        issues.push_back(issue);
    }
}

void validate_external_tool(const PreflightRequirement& req,
                              std::vector<PreflightIssue>& issues) {
    if (!tool_exists_in_path(req.path)) {
        PreflightIssue issue;
        issue.severity       = PreflightSeverity::Error;
        issue.type           = PreflightAssetType::ExternalTool;
        issue.code           = "MISSING_EXTERNAL_TOOL";
        issue.path           = req.path;
        issue.composition_id = req.composition_id;
        issue.message        = "External tool not found in PATH: '" + req.path + "'";
        issue.recommendation = "Install '" + req.path + "' and make sure it is available in PATH.";
        issues.push_back(issue);
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Main validation
// ---------------------------------------------------------------------------

std::vector<PreflightIssue> RenderPreflight::validate() const {
    std::vector<PreflightIssue> issues;

    for (const auto& req : m_requirements) {
        switch (req.type) {
            case PreflightAssetType::Image:
                validate_file_exists(req, "MISSING_IMAGE", PreflightAssetType::Image, issues);
                break;
            case PreflightAssetType::Video:
                validate_file_exists(req, "MISSING_VIDEO", PreflightAssetType::Video, issues);
                break;
            case PreflightAssetType::Font:
                validate_file_exists(req, "MISSING_FONT", PreflightAssetType::Font, issues);
                break;
            case PreflightAssetType::Audio:
                validate_file_exists(req, "MISSING_AUDIO", PreflightAssetType::Audio, issues);
                break;
            case PreflightAssetType::OutputPath:
                validate_output_writable(req, issues);
                break;
            case PreflightAssetType::ExternalTool:
                validate_external_tool(req, issues);
                break;
            default:
                break;
        }
    }

    // Auto-validate all assets registered in AssetRegistry
    for (const auto& asset : AssetRegistry::instance().assets()) {
        if (!std::filesystem::exists(asset.path)) {
            PreflightIssue issue;
            issue.severity = PreflightSeverity::Error;
            issue.type     = PreflightAssetType::RegisteredAsset;
            issue.code     = "MISSING_REGISTERED_ASSET";
            issue.path     = asset.path.string();
            issue.message  = "Missing registered asset: '" + asset.path.string() + "'";
            issue.recommendation = "Verify that the asset exists or that the assets root is mounted correctly.";
            issues.push_back(issue);
        }
    }

    return issues;
}

void RenderPreflight::validate_or_throw() {
    auto issues = validate();

    bool has_error = false;
    for (const auto& i : issues) {
        if (i.severity == PreflightSeverity::Error) {
            has_error = true;
            break;
        }
    }

    if (has_error) {
        throw ChrononAssetError(format_preflight_issues_text(issues));
    }
}

bool RenderPreflight::ok() const {
    auto issues = validate();
    for (const auto& i : issues) {
        if (i.severity == PreflightSeverity::Error) return false;
    }
    return true;
}

void RenderPreflight::clear() {
    m_requirements.clear();
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

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
