#include "vulkan_cuda_frame_importer.hpp"

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <chronon3d/backends/vulkan/cuda_nv12_surface_compositor.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext_cuda.h>
}

#include <array>
#include <stdexcept>

namespace chronon3d::backends::vulkan {
namespace {

class VulkanCudaFrameImportSession final : public media::NativeFrameImportSession {
public:
    VulkanCudaFrameImportSession(VulkanBackend& backend,
                                 runtime::RenderSurfaceRegistry& registry,
                                 CUcontext context)
        : backend_(backend), registry_(registry), context_(context) {}

    ~VulkanCudaFrameImportSession() override {
        for (auto& slot : slots_) {
            slot.compositor.reset();
            if (slot.surface != runtime::kInvalidRenderSurfaceHandle) {
                (void)backend_.release_surface(slot.surface);
                (void)registry_.release(slot.surface);
            }
        }
    }

    std::shared_ptr<Framebuffer> import(
        const media::NativeDecodedFrameView& view) override {
        if (!context_ || !view.frame || view.width == 0 || view.height == 0 ||
            (view.format != runtime::PixelFormat::Nv12 &&
             view.format != runtime::PixelFormat::P010)) return nullptr;
        const auto* frame = static_cast<const AVFrame*>(view.frame);
        if (!frame->data[0] || !frame->data[1]) return nullptr;

        auto& slot = slots_[next_slot_++ % slots_.size()];
        if (slot.surface != runtime::kInvalidRenderSurfaceHandle &&
            (!registry_.lookup(slot.surface) ||
             !backend_.is_native_surface_valid(slot.surface))) {
            slot.compositor.reset();
            (void)backend_.release_surface(slot.surface);
            (void)registry_.release(slot.surface);
            slot.surface = runtime::kInvalidRenderSurfaceHandle;
        }
        if (slot.surface == runtime::kInvalidRenderSurfaceHandle) {
            auto desc = media::native_decode_surface_desc(view.width, view.height);
            desc.color = view.color;
            slot.surface = registry_.create(desc);
            if (slot.surface == runtime::kInvalidRenderSurfaceHandle ||
                !backend_.create_cuda_external_surface(slot.surface, desc).ok()) {
                if (slot.surface != runtime::kInvalidRenderSurfaceHandle)
                    (void)registry_.release(slot.surface);
                slot.surface = runtime::kInvalidRenderSurfaceHandle;
                return nullptr;
            }
            try {
                slot.compositor = std::make_unique<CudaNv12SurfaceCompositor>(
                    backend_.export_cuda_external_memory(slot.surface), context_);
            } catch (...) {
                (void)backend_.release_surface(slot.surface);
                (void)registry_.release(slot.surface);
                slot.surface = runtime::kInvalidRenderSurfaceHandle;
                return nullptr;
            }
        }
        const auto format = view.format == runtime::PixelFormat::P010
            ? CudaYuvFormat::P010 : CudaYuvFormat::Nv12;
        const auto params = make_yuv_to_rgb_params(view.color, view.format);
        if (!slot.compositor || !slot.compositor->composite(
                reinterpret_cast<CUdeviceptr>(frame->data[0]), frame->linesize[0],
                reinterpret_cast<CUdeviceptr>(frame->data[1]), frame->linesize[1],
                view.width, view.height, nullptr, format, params) ||
            !backend_.prepare_cuda_surface_for_vulkan(slot.surface).ok()) return nullptr;

        auto result = std::make_shared<Framebuffer>(
            static_cast<int>(view.width), static_cast<int>(view.height),
            static_cast<Color*>(nullptr));
        result->set_surface_handle(slot.surface);
        return result;
    }

private:
    struct Slot {
        runtime::RenderSurfaceHandle surface{runtime::kInvalidRenderSurfaceHandle};
        std::unique_ptr<CudaNv12SurfaceCompositor> compositor;
    };
    VulkanBackend& backend_;
    runtime::RenderSurfaceRegistry& registry_;
    CUcontext context_;
    std::array<Slot, 4> slots_;
    std::size_t next_slot_{0};
};

} // namespace

VulkanCudaFrameImporter::VulkanCudaFrameImporter(
    VulkanBackend& backend, runtime::RenderSurfaceRegistry& registry,
    CUcontext cuda_context)
    : backend_(backend), registry_(registry), cuda_context_(cuda_context) {}

std::unique_ptr<media::NativeFrameImportSession>
VulkanCudaFrameImporter::create_session() {
    return std::make_unique<VulkanCudaFrameImportSession>(
        backend_, registry_, cuda_context_);
}

} // namespace chronon3d::backends::vulkan

#else

namespace chronon3d::backends::vulkan {
VulkanCudaFrameImporter::VulkanCudaFrameImporter(
    VulkanBackend& backend, runtime::RenderSurfaceRegistry& registry,
    CUcontext cuda_context)
    : backend_(backend), registry_(registry), cuda_context_(cuda_context) {}
std::unique_ptr<media::NativeFrameImportSession>
VulkanCudaFrameImporter::create_session() { return nullptr; }
} // namespace chronon3d::backends::vulkan

#endif
