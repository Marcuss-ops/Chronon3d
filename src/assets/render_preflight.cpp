#include <chronon3d/assets/render_preflight.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <filesystem>
#include <sstream>

namespace chronon3d {

void RenderPreflight::require_image(const std::string& path) {
    m_required_images.push_back(path);
}

void RenderPreflight::require_video(const std::string& path) {
    m_required_videos.push_back(path);
}

void RenderPreflight::validate_or_throw() {
    namespace fs = std::filesystem;
    std::vector<std::string> failures;

    // Validate images
    for (const auto& img_path : m_required_images) {
        std::string resolved = AssetRegistry::resolve(img_path);
        if (!fs::exists(resolved)) {
            std::ostringstream ss;
            ss << "Missing Image Asset: '" << img_path << "'\n"
               << "    -> Path resolved: '" << resolved << "'\n"
               << "    -> Recommendation: Make sure the file exists, or adjust AssetRegistry::mount() to the correct assets directory.";
            failures.push_back(ss.str());
        }
    }

    // Validate videos
    for (const auto& vid_path : m_required_videos) {
        std::string resolved = AssetRegistry::resolve(vid_path);
        if (!fs::exists(resolved)) {
            std::ostringstream ss;
            ss << "Missing Video Asset: '" << vid_path << "'\n"
               << "    -> Path resolved: '" << resolved << "'\n"
               << "    -> Recommendation: Check that the video file is present at the target path.";
            failures.push_back(ss.str());
        }
    }

    // Auto-validate all assets registered in AssetRegistry
    for (const auto& asset : AssetRegistry::instance().assets()) {
        if (!fs::exists(asset.path)) {
            std::ostringstream ss;
            ss << "Missing Registered Asset: '" << asset.path.string() << "'\n"
               << "    -> Path resolved: '" << asset.path.string() << "'\n"
               << "    -> Recommendation: Verify that the asset exists or that the assets root is mounted correctly.";
            failures.push_back(ss.str());
        }
    }

    if (!failures.empty()) {
        std::ostringstream err;
        err << "\n================================================================================\n"
            << "  [CHRONON3D ASSET ERROR] - Render preflight validation failed!\n"
            << "================================================================================\n";
        for (const auto& fail : failures) {
            err << "  " << fail << "\n\n";
        }
        err << "================================================================================\n";
        throw ChrononAssetError(err.str());
    }
}

void RenderPreflight::clear() {
    m_required_images.clear();
    m_required_videos.clear();
}

} // namespace chronon3d
