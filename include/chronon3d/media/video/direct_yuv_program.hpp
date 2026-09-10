#pragma once

#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/media/video/composition_capability.hpp>
#include <chronon3d/media/video/cuda_image_resource.hpp>
#include <chronon3d/media/video/direct_yuv_frame.hpp>
#include <chronon3d/media/video/hw_frame_ref.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d {
class ImageCache;
namespace media { class NativeVideoFrameDecoder; }
}

namespace chronon3d::media::video {

struct DirectLayerResourceEntry {
    std::shared_ptr<const ::chronon3d::media::CudaImageResource> gpu_resource;
    float native_width{0.0f};
    float native_height{0.0f};
    float local_offset_x{0.0f};
    float local_offset_y{0.0f};
    float corner_radius{0.0f};
};

struct DirectVideoLayer {
    std::string name;
    ::chronon3d::video::VideoSource source;
};

/// Prepared DirectYUV program. It owns immutable resources and one retained
/// decoder frame; the canonical CLI slot/writer pipeline owns frame looping,
/// cancellation, queue submission, and timing.
class DirectYuvProgram final {
public:
    struct CapabilityReport {
        bool eligible{false};
        DirectYuvSupport support{DirectYuvSupport::Unsupported};
        std::string reason;
    };

    [[nodiscard]] static CapabilityReport resolve_support(
        const CompiledComposition& compiled,
        DirectYuvPolicy policy);

    static std::shared_ptr<DirectYuvProgram> prepare(
        const CompiledComposition& compiled,
        ImageCache& image_cache,
        std::shared_ptr<::chronon3d::media::VideoDeviceRuntime> video_runtime,
        std::string& reason);

    [[nodiscard]] std::shared_ptr<DirectYuvFrame> execute(
        ::chronon3d::media::NativeVideoFrameDecoder& decoder,
        Frame frame) const;

    [[nodiscard]] const std::string& video_path() const noexcept { return video_path_; }
    [[nodiscard]] double scene_eval_ms() const noexcept { return scene_eval_ms_; }
    [[nodiscard]] double watermark_load_ms() const noexcept { return watermark_load_ms_; }
    [[nodiscard]] double watermark_upload_ms() const noexcept { return watermark_upload_ms_; }
    [[nodiscard]] std::uint64_t template_eval_count() const noexcept { return template_eval_count_; }
    [[nodiscard]] std::uint64_t template_reuse_count() const noexcept { return template_reuse_count_; }

private:
    DirectYuvProgram() = default;
    std::string video_path_;
    std::vector<DirectVideoLayer> video_layers_;
    int width_{0};
    int height_{0};
    std::shared_ptr<const Composition> composition_;
    std::unordered_map<std::string, DirectLayerResourceEntry> layer_resources_;
    std::vector<std::shared_ptr<const ::chronon3d::media::CudaImageResource>> persistent_resources_;
    double scene_eval_ms_{0.0};
    double watermark_load_ms_{0.0};
    double watermark_upload_ms_{0.0};
    mutable ::chronon3d::media::HwFrameRef last_decoded_{};

    bool all_overlay_layers_static_{false};
    struct CachedCueTemplate {
        Frame valid_from{-1};
        Frame valid_until{-1};
        std::shared_ptr<const DirectYuvTemplate> template_frame;
    };
    mutable CachedCueTemplate cached_template_{};
    mutable std::uint64_t template_eval_count_{0};
    mutable std::uint64_t template_reuse_count_{0};
};

} // namespace chronon3d::media::video
