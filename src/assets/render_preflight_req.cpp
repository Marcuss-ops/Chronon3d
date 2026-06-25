#include <chronon3d/assets/render_preflight.hpp>

#include <cstdlib>

namespace chronon3d {

bool tool_exists_in_path(const std::string& tool) {
    std::string cmd = "command -v " + tool + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
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
