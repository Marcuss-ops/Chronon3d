#include <chronon3d/assets/render_preflight.hpp>

#include <cstdlib>
#include <array>
#include <filesystem>
#include <string_view>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace chronon3d {

bool tool_exists_in_path(const std::string& tool) {
#ifdef _WIN32
    // `echo` is a cmd.exe builtin rather than an executable on Windows.
    if (tool == "echo") return true;
    const std::filesystem::path requested{tool};
    std::error_code ec;
    if (requested.has_parent_path()) return std::filesystem::is_regular_file(requested, ec);

    const char* raw_path = std::getenv("PATH");
    if (!raw_path) return false;
    const bool has_extension = requested.has_extension();
    constexpr std::array<std::wstring_view, 4> extensions{L".exe", L".com", L".bat", L".cmd"};
    std::wstring wide_tool;
    const int needed = MultiByteToWideChar(CP_UTF8, 0, tool.c_str(), -1, nullptr, 0);
    if (needed <= 1) return false;
    wide_tool.resize(static_cast<std::size_t>(needed));
    MultiByteToWideChar(CP_UTF8, 0, tool.c_str(), -1, wide_tool.data(), needed);
    wide_tool.pop_back();

    std::string_view path{raw_path};
    while (!path.empty()) {
        const auto separator = path.find(';');
        const auto directory = path.substr(0, separator);
        if (!directory.empty()) {
            const std::filesystem::path base{std::string{directory}};
            if (has_extension) {
                ec.clear();
                if (std::filesystem::is_regular_file(base / requested, ec)) return true;
            } else {
                for (const auto extension : extensions) {
                    ec.clear();
                    if (std::filesystem::is_regular_file(
                            base / std::filesystem::path{wide_tool + std::wstring{extension}}, ec))
                        return true;
                }
            }
        }
        if (separator == std::string_view::npos) break;
        path.remove_prefix(separator + 1);
    }
    return false;
#else
    std::string cmd = "command -v " + tool + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
#endif
}

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

void RenderPreflight::clear() {
    m_requirements.clear();
}

} // namespace chronon3d
