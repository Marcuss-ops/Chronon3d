#include "vulkan_cuda_frame_importer.hpp"

#include <chronon3d/media/video/native_frame_importer_factory.hpp>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <chronon3d/media/video/compositor/cuda/cuda_nv12_compositor.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_cuda.h>
}

#include <chronon3d/core/profiling/profiling.hpp>
#include <atomic>
#include <cstdlib>
#include <stdexcept>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string_view>
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

        // PERF-DISPATCH-01 — the native video import is the per-frame cost the
        // dispatch counter actually pays on decoded layers. The four phases
        // below are timed independently behind an opt-in env gate so the cost
        // can be attributed instead of guessed.
        static const bool diag_import = [] {
            const char* value = std::getenv("CHRONON3D_DIAG_IMPORT");
            if (!value) return false;
            const std::string_view setting(value);
            return !setting.empty() && setting != "0" && setting != "false" &&
                   setting != "off";
        }();
        // PERF-DISPATCH-02 — the four phases must be sampled at regime, not on
        // the first calls, which still pay one-time surface creation. With
        // CHRONON3D_DIAG_IMPORT_FRAMES="first-last" (inclusive, same syntax as
        // CHRONON3D_DIAG_EXEC_FRAMES) the window applies to the import-call
        // sequence, one call per rendered frame. Unset keeps the historical
        // first twelve calls. Parsed once, only when the gate is on.
        struct ImportWindow {
            std::int64_t first{-1};
            std::int64_t last{-1};
        };
        static const ImportWindow import_window = [] {
            ImportWindow window{};
            const char* value = std::getenv("CHRONON3D_DIAG_IMPORT_FRAMES");
            if (!value) return window;
            const std::string_view setting(value);
            if (setting.empty()) return window;
            const auto to_int = [](std::string_view text,
                                   std::int64_t fallback) noexcept {
                if (text.empty()) return fallback;
                std::int64_t parsed = 0;
                for (const char c : text) {
                    if (c < '0' || c > '9') return fallback;
                    parsed = parsed * 10 + (c - '0');
                }
                return parsed;
            };
            const auto dash = setting.find('-');
            window.first = dash == std::string_view::npos
                ? to_int(setting, window.first)
                : to_int(setting.substr(0, dash), window.first);
            window.last = dash == std::string_view::npos
                ? window.first
                : to_int(setting.substr(dash + 1), window.first);
            if (window.first < 0) return ImportWindow{};
            if (window.last < window.first) window.last = window.first;
            return window;
        }();
        static std::atomic<std::uint64_t> diag_calls{0};
        const std::uint64_t import_call =
            diag_import ? diag_calls.fetch_add(1) : 0;
        const bool diag = diag_import &&
            (import_window.first < 0
                 ? import_call < 12
                 : (static_cast<std::int64_t>(import_call) >= import_window.first &&
                    static_cast<std::int64_t>(import_call) <= import_window.last));
        const auto t_slot0 = diag ? profiling::now() : profiling::Clock::time_point{};
        auto& slot = slots_[next_slot_++ % slots_.size()];
        if (slot.pending) {
            // A slot is reusable only after both halves of the external
            // semaphore handoff completed.  Validity of the logical surface
            // alone is not an ownership/fence signal.
            // P1 (TICKET-PERF-P1): only the slots that recorded this surface
            // must be idle before CUDA rewrites it.  The former full-ring drain
            // stalled the five frames still in flight every ring turn.
            // PERF-DISPATCH-02 — the drain is the whole cost of the import
            // (conversion and handoff are ~0.05 ms), so the two halves are
            // timed separately: the Vulkan side (downstream render still using
            // the surface) and the CUDA side (the compositor stream, which the
            // conversion kernel shares with the decoder stream).
            const auto t_drain0 = diag ? profiling::now() : profiling::Clock::time_point{};
            const bool idle_ok = backend_.wait_for_surface_idle(slot.surface);
            const auto t_drain1 = diag ? profiling::now() : profiling::Clock::time_point{};
            const bool sync_ok = idle_ok && slot.compositor != nullptr &&
                                 slot.compositor->synchronize();
            const auto t_drain2 = diag ? profiling::now() : profiling::Clock::time_point{};
            if (!idle_ok || !slot.compositor || !sync_ok) {
                spdlog::error("[native-import] pending slot {} generation {} did not drain",
                              slot.index, slot.generation);
                return nullptr;
            }
            if (diag) {
                spdlog::info("[native-import-drain] slot={} idle_ms={:.3f} sync_ms={:.3f}",
                             slot.index, profiling::duration_ms(t_drain0, t_drain1),
                             profiling::duration_ms(t_drain1, t_drain2));
            }
            slot.pending = false;
        }
        const auto t_slot1 = diag ? profiling::now() : profiling::Clock::time_point{};
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
            // The CUDA import below decodes media transfer into the
            // canonical linear graphics domain. Do not advertise the source
            // transfer on the post-conversion RGBA surface.
            desc.format.transfer = runtime::TransferFunction::Linear;
            desc.format.matrix = runtime::ColorMatrix::Identity;
            desc.format.range = runtime::ColorRange::Full;
            desc.format.primaries = runtime::ColorPrimaries::Bt709;
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
                slot.compositor = std::make_unique<media::video::CudaNv12Compositor>(
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
            ? media::video::CudaYuvFormat::P010 : media::video::CudaYuvFormat::Nv12;
        const auto params = make_yuv_to_rgb_params(view.color, view.format);
        // NVDEC writes the CUDA frame on the stream published by FFmpeg's
        // CUDA device context.  The AVFrame timestamp becomes visible before
        // that stream's writes are necessarily complete.  Launching the
        // conversion on the compositor's unrelated stream therefore reads a
        // previously recycled decoder surface (the metadata says frame N,
        // the pixels are frame N-8).  Use the decoder stream as the producer
        // ordering boundary; the external C2V semaphore is then signalled
        // after both decode and conversion on the same stream.
        // FFmpeg leaves AVCUDADeviceContext::stream null for CUVID, which
        // means the legacy CUDA default stream rather than "no stream". The
        // compositor API uses null to select its private non-blocking stream,
        // so preserve the FFmpeg/default-stream ordering explicitly.
        CUstream decode_stream = CU_STREAM_LEGACY;
        if (frame->hw_frames_ctx) {
            auto* frames = reinterpret_cast<AVHWFramesContext*>(
                frame->hw_frames_ctx->data);
            if (frames && frames->device_ref) {
                auto* device = reinterpret_cast<AVHWDeviceContext*>(
                    frames->device_ref->data);
                auto* cuda = device
                    ? reinterpret_cast<AVCUDADeviceContext*>(device->hwctx)
                    : nullptr;
                if (cuda && cuda->stream) decode_stream = cuda->stream;
            }
        }
        if (!slot.compositor) {
            spdlog::error("[native-import] CUDA compositor unavailable");
            return nullptr;
        }
        const auto t_comp0 = diag ? profiling::now() : profiling::Clock::time_point{};
        const bool composed = slot.compositor->composite(
            reinterpret_cast<CUdeviceptr>(frame->data[0]), frame->linesize[0],
            reinterpret_cast<CUdeviceptr>(frame->data[1]), frame->linesize[1],
            view.width, view.height, decode_stream, format, params);
        const auto t_comp1 = diag ? profiling::now() : profiling::Clock::time_point{};
        if (!composed || !backend_.prepare_cuda_surface_for_vulkan(slot.surface).ok()) {
            spdlog::error("[native-import] CUDA NV12 composite or Vulkan sync failed");
            return nullptr;
        }
        if (diag) {
            spdlog::info(
                "[native-import-diag] slot={} drain_ms={:.3f} setup_ms={:.3f} "
                "composite_ms={:.3f} handoff_ms={:.3f}",
                slot.index, profiling::duration_ms(t_slot0, t_slot1),
                profiling::duration_ms(t_slot1, t_comp0),
                profiling::duration_ms(t_comp0, t_comp1),
                profiling::duration_ms(t_comp1, profiling::now()));
        }
        slot.pending = true;
        ++slot.generation;

        auto result = std::make_shared<Framebuffer>(
            static_cast<int>(view.width), static_cast<int>(view.height),
            static_cast<Color*>(nullptr));
        result->set_surface_handle(slot.surface);
        // The native surface owns the decoded pixels; the empty CPU shadow
        // must not make acquire_owned_fb() treat this frame as CPU-authoritative
        // and drop the handle before MaskNode can download it.
        result->mark_gpu_authoritative();
        return result;
    }

private:
    struct Slot {
        std::size_t index{0};
        runtime::RenderSurfaceHandle surface{runtime::kInvalidRenderSurfaceHandle};
        std::unique_ptr<media::video::CudaNv12Compositor> compositor;
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
