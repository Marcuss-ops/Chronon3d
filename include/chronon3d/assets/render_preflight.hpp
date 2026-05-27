#pragma once

#include <chronon3d/core/enum_utils.hpp>
#include <string>
#include <vector>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace chronon3d {

class ChrononAssetError : public std::runtime_error {
public:
    explicit ChrononAssetError(const std::string& message) : std::runtime_error(message) {}
};

// ---------------------------------------------------------------------------
// Preflight data model
// ---------------------------------------------------------------------------

enum class PreflightSeverity {
    Info,
    Warning,
    Error
};

enum class PreflightAssetType {
    Image,
    Video,
    Font,
    Audio,
    OutputPath,
    Directory,
    ExternalTool,
    RegisteredAsset
};

[[nodiscard]] inline std::string to_string(PreflightSeverity severity) {
    return enum_utils::enum_name_upper_snake(severity);
}

[[nodiscard]] inline std::string to_string(PreflightAssetType type) {
    return enum_utils::enum_name_lower_snake(type);
}

struct PreflightRequirement {
    PreflightAssetType type{PreflightAssetType::Image};
    std::string path;
    std::string resolved_path;
    std::string composition_id;
    std::string layer_id;
    std::string node_id;
    int frame{-1};
    bool required{true};
};

struct PreflightIssue {
    PreflightSeverity severity{PreflightSeverity::Error};
    PreflightAssetType type{PreflightAssetType::Image};
    std::string code;
    std::string message;
    std::string path;
    std::string resolved_path;
    std::string composition_id;
    std::string layer_id;
    std::string recommendation;
};

// ---------------------------------------------------------------------------
// RenderPreflight
// ---------------------------------------------------------------------------

class RenderPreflight {
public:
    static RenderPreflight& instance() {
        static RenderPreflight inst;
        return inst;
    }

    // --- Requirements (call before validate) ---
    void require_image(const std::string& path);
    void require_video(const std::string& path);
    void require_font(const std::string& path);
    void require_audio(const std::string& path);
    void require_output_path(const std::string& path);
    void require_external_tool(const std::string& name);
    void add_requirements(const std::vector<PreflightRequirement>& reqs);

    // --- Validation ---
    /// Collect all issues without throwing. Callers can inspect the list or
    /// produce JSON output before deciding what to do.
    [[nodiscard]] std::vector<PreflightIssue> validate() const;

    /// Behaves exactly like the original: throws ChrononAssetError if any error
    /// is found. Backward-compatible.
    void validate_or_throw();

    /// Returns true when validate() would return an empty vector.
    [[nodiscard]] bool ok() const;

    void clear();

private:
    RenderPreflight() = default;
    ~RenderPreflight() = default;
    RenderPreflight(const RenderPreflight&) = delete;
    RenderPreflight& operator=(const RenderPreflight&) = delete;

    std::vector<PreflightRequirement> m_requirements;
};

// --- Helpers (free functions) ---

/// Convert a list of issues to a human-readable text block (terminal output).
std::string format_preflight_issues_text(const std::vector<PreflightIssue>& issues);

/// Convert a list of issues to a structured JSON object.
nlohmann::json preflight_issues_to_json(const std::vector<PreflightIssue>& issues);

/// Check whether an external tool exists in PATH.
bool tool_exists_in_path(const std::string& tool);

} // namespace chronon3d
