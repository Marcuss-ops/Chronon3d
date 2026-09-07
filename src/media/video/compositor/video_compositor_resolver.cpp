#include <chronon3d/media/video/video_compositor_resolver.hpp>

namespace chronon3d::media::video {

void VideoCompositorRegistry::register_factory(std::string_view name, Factory factory) {
    entries_.push_back({std::string(name), std::move(factory)});
}

std::unique_ptr<VideoCompositor> VideoCompositorRegistry::create(
    const VideoCompositorCapabilities& capabilities,
    const VideoExecutionRequirements& requirements) const {
    if (requirements.require_cuda && !capabilities.cuda_nv12) return {};
    for (const auto& entry : entries_) {
        if (entry.name == "cuda_nv12") return entry.factory();
    }
    return {};
}

std::unique_ptr<VideoCompositor> resolve_video_compositor(
    const VideoCompositorRegistry& registry,
    const VideoCompositorCapabilities& capabilities,
    const VideoExecutionRequirements& requirements) {
    return registry.create(capabilities, requirements);
}

} // namespace chronon3d::media::video
