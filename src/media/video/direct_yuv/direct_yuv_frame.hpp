#pragma once

#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <chronon3d/media/video/cuda_layer_resource.hpp>
#include <chronon3d/media/video/cuda_image_resource.hpp>
#include <chronon3d/media/video/hw_frame_ref.hpp>

#include <memory>
#include <vector>

namespace chronon3d::cli {

/// Precompiled, frame-invariant Direct-YUV overlay IR.  Owned once by the
/// program and shared by every decoded frame: batch/resource tables and the
/// device-resident overlay image are never rebuilt or heap-copied per frame.
struct DirectYuvTemplate {
    runtime::GpuLayerBatch batch;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    std::vector<media::CudaLayerResource> resources;
    // Keeps the device-resident overlay memory alive as long as any frame
    // built from this template is still in flight.  Typed replacement for
    // the old `std::shared_ptr<void> resources_owner`.
    std::shared_ptr<const media::CudaImageResource> resource_owner;
    std::vector<std::shared_ptr<const media::CudaImageResource>> resource_owners;
#endif
};

/// One decoded CUDA NV12 frame plus a shared pointer to the frame-invariant
/// overlay IR.  Each frame is now just an HwFrameRef + one shared_ptr to the
/// template instead of a per-frame heap copy of batch/resources.
struct DirectYuvFrame {
    media::HwFrameRef decoded;
    // Additional native NV12 sources used as GPU-composited layers. Their
    // AVFrame ownership is kept until the writer has consumed the frame.
    std::vector<media::HwFrameRef> video_layers;
    std::shared_ptr<const DirectYuvTemplate> program;
};

} // namespace chronon3d::cli
