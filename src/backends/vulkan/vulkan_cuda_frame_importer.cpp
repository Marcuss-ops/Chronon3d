#include "vulkan_cuda_frame_importer.hpp"

#include <chronon3d/media/video/native_frame_importer_factory.hpp>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <chronon3d/backends/vulkan/cuda_nv12_surface_compositor.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext_cuda.h>
}

#include <stdexcept>
#include <mutex>
#include <spdlog/spdlog.h>
#include <vector>

namespace chronon3d::backends::vulkan {
namespace {

class VulkanCudaFrameImportSession final : public media::NativeFrameImportSession {
public:
    VulkanCudaFrameImportSession(VulkanBackend& backend,
                                 runtime::RenderSurfaceRegistry& registry,
                                 CUcontext context)
        : backend_(backend), registry_(registry), context_(context),
          slots_(backend.native_surface_ring_capacity()) {
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            slots_[index].index = index;
        }
    }

    ~VulkanCudaFrameImportSession() override {
        (void)backend_.wait_for_pending_submissions();
        for (auto& slot : slots_) {
            if (slot.pending && slot.compositor) (void)slot.compositor->synchronize();
            slot.compositor.reset();
            if (slot.surface != runtime::kInvalidRenderSurfaceHandle) {
                (void)backend_.release_surface(slot.surface);
                (void)registry_.release(slot.surface);
            }
        }
    }

    std::shared_ptr<Framebuffer> import(
        const media::NativeDecodedFrameView& view) override {
        std::lock_guard lock(mutex_);
        if (!context_ || !view.frame || view.width == 0 || view.height == 0 ||
            (view.format != runtime::PixelFormat::Nv12 &&
             view.format != runtime::PixelFormat::P010)) return nullptr;
        const auto* frame = static_cast<const AVFrame*>(view.frame);
        if (!frame->data[0] || !frame->data[1]) return nullptr;

        auto& slot = slots_[next_slot_++ % slots_.size()];
        if (slot.pending) {
            // A slot is reusable only after both halves of the external
            // semaphore handoff completed.  Validity of the logical surface
            // alone is not an ownership/fence signal.
            if (!backend_.wait_for_pending_submissions() ||
                !slot.compositor || !slot.compositor->synchronize()) {
                spdlog::error("[native-import] pending slot {} generation {} did not drain",
                              slot.index, slot.generation);
                return nullptr;
            }
            slot.pending = false;
        }
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
            desc.format.matrix = view.color.matrix;
            desc.format.range = view.color.range;
            desc.format.primaries = view.color.primaries;
            desc.format.transfer = view.color.transfer;
            slot.surface = registry_.create(desc);
            if (slot.surface == runtime::kInvalidRenderSurfaceHandle) {
                spdlog::error("[native-import] render surface allocation failed ({}x{})",
                              view.width, view.height);
                return nullptr;
            }
            const auto created = backend_.create_cuda_external_surface(slot.surface, desc);
            if (!created.ok()) {
                spdlog::error("[native-import] CUDA external surface creation failed: {}",
                              created.error().message);
                if (slot.surface != runtime::kInvalidRenderSurfaceHandle)
                    (void)registry_.release(slot.surface);
                slot.surface = runtime::kInvalidRenderSurfaceHandle;
                return nullptr;
            }
            try {
                slot.compositor = std::make_unique<CudaNv12SurfaceCompositor>(
                    backend_.export_cuda_external_memory(slot.surface), context_);
            } catch (const std::exception& error) {
                spdlog::error("[native-import] CUDA compositor creation failed: {}",
                              error.what());
                (void)backend_.release_surface(slot.surface);
                (void)registry_.release(slot.surface);
                slot.surface = runtime::kInvalidRenderSurfaceHandle;
                return nullptr;
            }
        }
        const auto format = view.format == runtime::PixelFormat::P010
            ? CudaYuvFormat::P010 : CudaYuvFormat::Nv12;
        const auto params = make_yuv_to_rgb_params(view.color, view.format);
        if (!slot.compositor) {
            spdlog::error("[native-import] CUDA compositor unavailable");
            return nullptr;
        }
        if (!slot.compositor->composite(
                reinterpret_cast<CUdeviceptr>(frame->data[0]), frame->linesize[0],
                reinterpret_cast<CUdeviceptr>(frame->data[1]), frame->linesize[1],
                view.width, view.height, nullptr, format, params) ||
            !backend_.prepare_cuda_surface_for_vulkan(slot.surface).ok()) {
            spdlog::error("[native-import] CUDA NV12 composite or Vulkan sync failed");
            return nullptr;
        }
        slot.pending = true;
        ++slot.generation;

        auto result = std::make_shared<Framebuffer>(
            static_cast<int>(view.width), static_cast<int>(view.height),
            static_cast<Color*>(nullptr));
        result->set_surface_handle(slot.surface);
        return result;
    }

private:
    struct Slot {
        std::size_t index{0};
        runtime::RenderSurfaceHandle surface{runtime::kInvalidRenderSurfaceHandle};
        std::unique_ptr<CudaNv12SurfaceCompositor> compositor;
        std::uint64_t generation{0};
        bool pending{false};
    };
    VulkanBackend& backend_;
    runtime::RenderSurfaceRegistry& registry_;
    CUcontext context_;
    std::vector<Slot> slots_;
    std::size_t next_slot_{0};
    std::mutex mutex_;
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

namespace chronon3d::media {

std::shared_ptr<NativeFrameImporter>
create_native_frame_importer_for_backend(
    graph::RenderBackend& backend,
    runtime::RenderSurfaceRegistry& registry,
    void* cuda_context) {
    auto* vulkan = dynamic_cast<backends::vulkan::VulkanBackend*>(&backend);
    if (!vulkan || !cuda_context) return nullptr;
    return std::make_shared<backends::vulkan::VulkanCudaFrameImporter>(
        *vulkan, registry, reinterpret_cast<CUcontext>(cuda_context));
}

} // namespace chronon3d::media

#else

namespace chronon3d::backends::vulkan {
VulkanCudaFrameImporter::VulkanCudaFrameImporter(
    VulkanBackend& backend, runtime::RenderSurfaceRegistry& registry,
    CUcontext cuda_context)
    : backend_(backend), registry_(registry), cuda_context_(cuda_context) {}
std::unique_ptr<media::NativeFrameImportSession>
VulkanCudaFrameImporter::create_session() { return nullptr; }
} // namespace chronon3d::backends::vulkan

namespace chronon3d::media {
std::shared_ptr<NativeFrameImporter>
create_native_frame_importer_for_backend(
    graph::RenderBackend&, runtime::RenderSurfaceRegistry&, void*) {
    return nullptr;
}
} // namespace chronon3d::media

#endif
