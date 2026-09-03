#pragma once

#include "../../../utils/video/direct_yuv_frame.hpp"

#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>
#include <chronon3d/media/video/cuda_image_resource.hpp>
#include <chronon3d/backends/video/video_source.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d {
class ImageCache;
namespace media {
class NativeVideoFrameDecoder;
// The resource type is CUDA-only at implementation level, but the public
// program holder also needs to compile in the non-CUDA video preset.
class CudaImageResource;
}
}

namespace chronon3d::cli {

struct DirectLayerResourceEntry {
    std::shared_ptr<const media::CudaImageResource> gpu_resource;
    float native_width{0.0f};
    float native_height{0.0f};
    float local_offset_x{0.0f};
    float local_offset_y{0.0f};
    float corner_radius{0.0f};
};

struct DirectVideoLayer {
    std::string name;
    video::VideoSource source;
};

/// Resolver-owned, precompiled program for the direct CUDA NV12 compositor.
/// Supports 2D image overlays (scale, translate, opacity) and single-pass
/// pre-rasterized text textures with zero CPU readback.
class DirectYuvProgram final {
public:
    static std::shared_ptr<DirectYuvProgram> prepare(
        const CompiledComposition& compiled,
        ImageCache& image_cache,
        std::shared_ptr<media::VideoDeviceRuntime> video_runtime,
        std::string& reason);

    [[nodiscard]] std::shared_ptr<DirectYuvFrame> execute(
        media::NativeVideoFrameDecoder& decoder,
        Frame frame) const;

    [[nodiscard]] const std::string& video_path() const noexcept { return video_path_; }
    [[nodiscard]] double scene_eval_ms() const noexcept { return scene_eval_ms_; }
    [[nodiscard]] double watermark_load_ms() const noexcept { return watermark_load_ms_; }
    [[nodiscard]] double watermark_upload_ms() const noexcept { return watermark_upload_ms_; }

private:
    DirectYuvProgram() = default;
    std::string video_path_;
    std::vector<DirectVideoLayer> video_layers_;
    int width_{0};
    int height_{0};
    std::shared_ptr<const Composition> composition_;
    std::unordered_map<std::string, DirectLayerResourceEntry> layer_resources_;
    std::vector<std::shared_ptr<const media::CudaImageResource>> persistent_resources_;
    double scene_eval_ms_{0.0};
    double watermark_load_ms_{0.0};
    double watermark_upload_ms_{0.0};
    mutable media::HwFrameRef last_decoded_{};
};

} // namespace chronon3d::cli
