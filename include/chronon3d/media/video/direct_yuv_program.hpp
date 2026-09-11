#pragma once

#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/media/video/composition_capability.hpp>
#include <chronon3d/media/video/cuda_image_resource.hpp>
#include <chronon3d/media/video/direct_yuv_frame.hpp>
#include <chronon3d/media/video/hw_frame_ref.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>
#include <chronon3d/media/video/cuda_layer_resource.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
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
    float corner_radius{0.0f};
    media::CudaLayerResourceKind kind{media::CudaLayerResourceKind::Rgba};
    float distance_range_px{8.0f};
    std::uint32_t color{0xFFFFFFFFu};
    std::uint32_t stroke_color{0};
    float stroke_width{0.0f};
    // AUDIT-10: local_offset_x/local_offset_y removed (0 readers repo-wide).
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

    /// AUDIT-04A: execute() is deliberately NON-const. It mutates the
    /// retained decoder frame and the cue-template cache, so pretending it
    /// was a const accessor (mutable members) broke the type's contract and
    /// made thread-safety claims unenforceable.
    [[nodiscard]] std::shared_ptr<DirectYuvFrame> execute(
        ::chronon3d::media::NativeVideoFrameDecoder& decoder,
        Frame frame);

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
    /// AUDIT-04B: allocation-free per-frame lookup index over layer_resources_.
    /// string_views point at the map's stable node keys (filled once in
    /// prepare(), map is read-only afterwards).
    std::vector<std::pair<std::string_view, const DirectLayerResourceEntry*>> resource_index_;
    [[nodiscard]] const DirectLayerResourceEntry* find_resource(std::string_view name) const noexcept;
    std::vector<std::shared_ptr<const ::chronon3d::media::CudaImageResource>> persistent_resources_;
    double scene_eval_ms_{0.0};
    double watermark_load_ms_{0.0};
    double watermark_upload_ms_{0.0};
    bool all_overlay_layers_static_{false};
    struct CachedCueTemplate {
        Frame valid_from{-1};
        Frame valid_until{-1};
        std::shared_ptr<const DirectYuvTemplate> template_frame;
    };
    CachedCueTemplate cached_template_{};
    std::uint64_t template_eval_count_{0};
    std::uint64_t template_reuse_count_{0};
    /// Reusable frame-template objects. A returned template remains leased by
    /// the encoder, so reuse is allowed only while the pool is the sole owner.
    /// This preserves frame lifetime while avoiding three fresh vector
    /// allocations for every dynamic frame.
    std::vector<std::shared_ptr<DirectYuvTemplate>> dynamic_template_pool_;
    [[nodiscard]] std::shared_ptr<DirectYuvTemplate> acquire_dynamic_template();

    /// Reclaim only sole-owned wrappers; the hard bound keeps multi-job
    /// exports from growing an unbounded frame-object pool.
    std::vector<std::shared_ptr<DirectYuvFrame>> frame_pool_;
    [[nodiscard]] std::shared_ptr<DirectYuvFrame> acquire_frame();

    /// Monotonic high-water mark of instances per dynamic frame; seeds the
    /// first allocation of a newly-created template.
    std::size_t overlay_instances_hint_{8};
};

} // namespace chronon3d::media::video
