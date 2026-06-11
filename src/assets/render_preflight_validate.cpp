#include <chronon3d/assets/render_preflight.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include "render_preflight_helpers.hpp"

#include <filesystem>
#include <fstream>

namespace chronon3d {

namespace {

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

    if (!fs::exists(parent)) {
        fs::path check_parent = parent;
        while (!check_parent.empty() && !fs::exists(check_parent)) {
            check_parent = check_parent.parent_path();
        }

        bool can_create = !check_parent.empty() && fs::exists(check_parent);
        bool ancestor_writable = false;
        if (can_create) {
            auto tmp = check_parent / ".chronon3d_preflight_test";
            std::ofstream test(tmp);
            if (test.is_open()) {
                test.close();
                std::error_code ec;
                fs::remove(tmp, ec);
                ancestor_writable = true;
            }
        }

        PreflightIssue issue;
        issue.type           = PreflightAssetType::OutputPath;
        issue.path           = req.path;
        issue.composition_id = req.composition_id;
        issue.layer_id       = req.layer_id;

        if (can_create && ancestor_writable) {
            issue.severity       = PreflightSeverity::Warning;
            issue.code           = "OUTPUT_DIR_MISSING";
            issue.message        = "Output directory does not exist: '" + parent.string() + "'";
            issue.recommendation = "The render pipeline will attempt to create it. Ensure the parent path is writable.";
        } else {
            issue.severity       = PreflightSeverity::Error;
            issue.code           = "OUTPUT_DIR_UNWRITABLE_ANCESTOR";
            issue.message        = "Output directory does not exist and nearest ancestor is not writable: '" + parent.string() + "'";
            issue.recommendation = "Check filesystem permissions or create the directory manually.";
        }
        issues.push_back(issue);
        return;
    }

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
    throw_if_preflight_errors(validate());
}

bool RenderPreflight::ok() const {
    return !has_preflight_errors(validate());
}

} // namespace chronon3d
