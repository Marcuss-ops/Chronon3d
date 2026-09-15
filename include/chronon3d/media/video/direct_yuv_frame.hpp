#pragma once

#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <chronon3d/media/video/cuda_layer_resource.hpp>
#include <chronon3d/media/video/cuda_image_resource.hpp>
#include <chronon3d/media/video/hw_frame_ref.hpp>

#include <memory>
#include <vector>

namespace chronon3d::media::video {

/// Frame-invariant DirectYUV overlay IR owned by the media execution path.
struct DirectYuvTemplate {
    runtime::GpuLayerBatch batch;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    std::vector<::chronon3d::media::CudaLayerResource> resources;
    std::shared_ptr<const ::chronon3d::media::CudaImageResource> resource_owner;
    std::vector<std::shared_ptr<const ::chronon3d::media::CudaImageResource>> resource_owners;
#endif

    void reset_for_reuse() noexcept {
        batch.instances.clear();
        batch.resources.clear();
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        resources.clear();
        resource_owner.reset();
        resource_owners.clear();
#endif
    }
};

/// Native decoded frame plus the DirectYUV media program required by the
/// encoder. No CLI/export-session ownership is carried by this type.
struct DirectYuvFrame {
    ::chronon3d::media::HwFrameRef decoded;
    std::vector<::chronon3d::media::HwFrameRef> video_layers;
    std::shared_ptr<const DirectYuvTemplate> program;
    // Facts captured by DirectYuvProgram::execute(), consumed by the
    // exporter telemetry projection. These are deliberately per-frame rather
    // than reconstructed from cumulative counters in the CLI.
    double decode_ms{0.0};
    bool template_reused{false};
};

} // namespace chronon3d::media::video
