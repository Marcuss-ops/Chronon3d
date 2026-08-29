#pragma once

#include <chronon3d/media/video/native_frame_importer.hpp>
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

#include <memory>

namespace chronon3d::backends::vulkan {

/// Creates importer sessions that own Vulkan/CUDA native decode resources.
class VulkanCudaFrameImporter final : public media::NativeFrameImporter {
public:
    VulkanCudaFrameImporter(VulkanBackend& backend,
                            runtime::RenderSurfaceRegistry& registry,
                            CUcontext cuda_context);
    ~VulkanCudaFrameImporter() override = default;

    [[nodiscard]] void* cuda_context() const noexcept override { return cuda_context_; }

    [[nodiscard]] std::unique_ptr<media::NativeFrameImportSession>
    create_session() override;

private:
    VulkanBackend& backend_;
    runtime::RenderSurfaceRegistry& registry_;
    CUcontext cuda_context_{nullptr};
};

} // namespace chronon3d::backends::vulkan
