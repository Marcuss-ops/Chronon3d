#pragma once

#include <chronon3d/runtime/gpu_runtime.hpp>
#include <chronon3d/runtime/device_scheduler.hpp>
#include <chronon3d/runtime/frame/frame_execution_slot.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/backends/image/image_backend.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>

struct AVBufferRef;

namespace chronon3d::graph { class RenderBackend; }

namespace chronon3d::media {

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
struct CudaImageResource;
#endif

/// Process-persistent GPU device runtime for the video pipeline.
///
/// Owns exactly ONE FFmpeg CUDA hwdevice per device, created with
/// AV_CUDA_USE_PRIMARY_CONTEXT so it aliases the same CUDA primary context
/// retained by runtime::GpuRuntime, verified by a fail-closed assertion at
/// first use. Everything that touches a GPU (NVENC, NVDEC, CUDA compositors)
/// borrows refs from here instead of creating its own driver context. FFmpeg
/// objects stay out of GpuRuntime; the libav* dependency
/// lives here in the media layer.
class VideoDeviceRuntime {
public:    /// Creates a runtime for `device`. If `gpu` is null a new GpuRuntime is

    /// created (but not initialized; CUDA init is lazy and happens on the
    /// first `ref_cuda_hwdevice()`).    `reason` carries the fail message on
    /// return of null.
    static std::shared_ptr<VideoDeviceRuntime> create(
        runtime::DeviceId device,
        std::shared_ptr<runtime::GpuRuntime> gpu,
        std::string& reason,
        std::int32_t cuda_device_ordinal = -1);

    ~VideoDeviceRuntime();

    VideoDeviceRuntime(const VideoDeviceRuntime&) = delete;
    VideoDeviceRuntime& operator=(const VideoDeviceRuntime&) = delete;

    [[nodiscard]] runtime::DeviceId device_id() const noexcept { return device_; }

    /// Worker-owned decoded image cache. It is shared by all DirectYuv jobs
    /// using this persistent per-device runtime.
    [[nodiscard]] ImageCache& image_cache() noexcept { return image_cache_; }
    [[nodiscard]] const ImageCache& image_cache() const noexcept { return image_cache_; }

    /// The GpuRuntime backing this device (null only if creation/init failed
    /// in a CUDA-less build). Callers check null as FAIL_CLOSED. Never
    /// initializes — use ref_cuda_hwdevice() to trigger initialization.
    [[nodiscard]] std::shared_ptr<runtime::GpuRuntime> gpu() const noexcept {
        return gpu_;
    }

    /// Borrow the persistent CUDA hwdevice: returns a NEW AVBufferRef the
    /// caller owns (av_buffer_unref), or null when CUDA is unavailable or the
    /// FAIL_CLOSED assertion failed. Creates the hwdevice on first use.
    AVBufferRef* ref_cuda_hwdevice();

    /// Acquire the backend surfaces owned by an execution slot. Format,
    /// allocation size, lifetime and partial-failure cleanup stay here;
    /// callers only provide the engine-owned registry/backend and dimensions.
    bool acquire_slot_surfaces(
        runtime::FrameExecutionSlot& slot,
        runtime::RenderSurfaceRegistry& registry,
        graph::RenderBackend& backend,
        std::uint32_t width,
        std::uint32_t height,
        std::string& reason);

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    /// Borrow a persistent FFmpeg CUDA frames context for one exact surface
    /// contract. The registry owns it for the lifetime of this device; each
    /// caller receives its own AVBufferRef and must unref it.
    AVBufferRef* ref_cuda_frames(
        std::uint32_t width, std::uint32_t height, int sw_format,
        std::string& reason);
#endif

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    /// Return a device-resident copy of an image, cached by content and
    /// decode options for this device runtime. The returned resource owns a
    /// reference to the CUDA runtime, so destruction is context-safe.
    std::shared_ptr<const CudaImageResource> get_or_upload_image(
        const assets::ContentDigest& digest,
        const ImageDecodeOptions& options,
        std::uint32_t width,
        std::uint32_t height,
        std::span<const float> rgba,
        bool& cache_hit,
        double& upload_ms,
        std::string& reason);
#endif

    /// Pure contract check used by decoder/encoder and by tests. Returns false
    /// for null handles or a context mismatch; callers must fail closed.
    [[nodiscard]] bool context_matches(std::uintptr_t context) const noexcept;

private:
    VideoDeviceRuntime(runtime::DeviceId device,
                       std::shared_ptr<runtime::GpuRuntime> gpu,
                       std::int32_t cuda_device_ordinal);

    /// Lazily initialize GpuRuntime + the FFmpeg CUDA hwdevice.    Returns
    /// false (and sets `reason`) on any initialization failure — callers must
    /// treat that as FAIL_CLOSED.
    bool ensure_initialized(std::string& reason);

    runtime::DeviceId device_;
    std::int32_t cuda_device_ordinal_{-1};
    std::shared_ptr<runtime::GpuRuntime> gpu_;
    AVBufferRef* cuda_hwdevice_{nullptr};
    bool initialized_{false};
    bool init_ok_{false};
    std::mutex mutex_;
    ImageCache image_cache_;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    struct CudaFramesKey {
        std::uint32_t width{0};
        std::uint32_t height{0};
        int sw_format{0};
        friend bool operator==(const CudaFramesKey&, const CudaFramesKey&) = default;
    };
    struct CudaFramesKeyHash {
        std::size_t operator()(const CudaFramesKey& key) const noexcept {
            std::size_t h = key.width;
            h = h * 31U + key.height;
            h = h * 31U + static_cast<std::size_t>(key.sw_format);
            return h;
        }
    };
    std::unordered_map<CudaFramesKey, AVBufferRef*, CudaFramesKeyHash> cuda_frames_;
#endif
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    struct CudaImageKey {
        assets::ContentDigest digest{};
        ImageDecodeOptions options{};
        std::uint32_t width{0};
        std::uint32_t height{0};
        friend bool operator==(const CudaImageKey&, const CudaImageKey&) = default;
    };
    struct CudaImageKeyHash {
        std::size_t operator()(const CudaImageKey& key) const noexcept {
            std::size_t hash = 0;
            for (const auto byte : key.digest.bytes) {
                hash ^= static_cast<std::size_t>(std::to_integer<unsigned char>(byte)) +
                    static_cast<std::size_t>(0x9e3779b9u) + (hash << 6u) + (hash >> 2u);
            }
            hash ^= static_cast<std::size_t>(key.options.color_space) +
                (hash << 6u) + (hash >> 2u);
            hash ^= static_cast<std::size_t>(key.options.premultiply) +
                (hash << 6u) + (hash >> 2u);
            hash ^= static_cast<std::size_t>(key.options.orientation) +
                (hash << 6u) + (hash >> 2u);
            hash ^= key.width + (hash << 6u) + (hash >> 2u);
            hash ^= key.height + (hash << 6u) + (hash >> 2u);
            return hash;
        }
    };
    std::unordered_map<CudaImageKey,
        std::shared_ptr<const CudaImageResource>, CudaImageKeyHash> cuda_images_;
#endif
};

/// One registry per engine/daemon: get_or_create(device) returns the single
/// VideoDeviceRuntime owning that device's CUDA context + FFmpeg hwdevice.
/// The daemon owns one of these next to its DeviceScheduler; callers must
/// retain and pass the same registry across jobs to reuse the device runtime.
class VideoRuntimeRegistry {
public:
    VideoRuntimeRegistry() = default;
    ~VideoRuntimeRegistry() = default;

    VideoRuntimeRegistry(const VideoRuntimeRegistry&) = delete;
    VideoRuntimeRegistry& operator=(const VideoRuntimeRegistry&) = delete;

    /// Returns the persistent runtime for `device`, creating it on first use
    /// (lazy CUDA init). Null only on hard failure (logged). If a
    /// GpuRuntime is supplied, it is reused for every VideoDeviceRuntime in
    /// this registry instead of creating one owner per request.
    std::shared_ptr<VideoDeviceRuntime> get_or_create(
        runtime::DeviceId device,
        std::shared_ptr<runtime::GpuRuntime> gpu = nullptr,
        std::int32_t cuda_device_ordinal = -1);

    [[nodiscard]] std::size_t size() const noexcept;

private:
    mutable std::mutex mutex_;
    std::unordered_map<runtime::DeviceId,
        std::shared_ptr<VideoDeviceRuntime>> runtimes_;
    // Optional engine-owned GPU runtimes, one per device. Keeping these here
    // makes the registry the single owner boundary for the video runtime.
    std::unordered_map<runtime::DeviceId,
        std::shared_ptr<runtime::GpuRuntime>> gpu_runtimes_;
    std::unordered_map<runtime::DeviceId, std::int32_t> cuda_ordinals_;
};

} // namespace chronon3d::media
