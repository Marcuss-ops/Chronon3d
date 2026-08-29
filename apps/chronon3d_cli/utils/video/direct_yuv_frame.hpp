#pragma once

#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <chronon3d/media/video/cuda_layer_resource.hpp>

#include <memory>
#include <vector>

struct AVFrame;

namespace chronon3d::cli {

/// One decoded CUDA NV12 frame plus the precompiled direct-YUV overlay IR.
/// `resources_owner` keeps device-resident overlay memory alive until the
/// asynchronous NVENC handoff has consumed the frame.
struct DirectYuvFrame {
    std::shared_ptr<AVFrame> decoded;
    runtime::GpuLayerBatch batch;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    std::vector<media::CudaLayerResource> resources;
#endif
    std::shared_ptr<void> resources_owner;
};

} // namespace chronon3d::cli
