#pragma once

#include <chronon3d/runtime/gpu_runtime.hpp>
#include <chronon3d/runtime/device_scheduler.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

struct AVBufferRef;

namespace chronon3d::media {

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
        std::string& reason);

    ~VideoDeviceRuntime();

    VideoDeviceRuntime(const VideoDeviceRuntime&) = delete;
    VideoDeviceRuntime& operator=(const VideoDeviceRuntime&) = delete;

    [[nodiscard]] runtime::DeviceId device_id() const noexcept { return device_; }

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

    /// Pure contract check used by decoder/encoder and by tests. Returns false
    /// for null handles or a context mismatch; callers must fail closed.
    [[nodiscard]] bool context_matches(std::uintptr_t context) const noexcept;

private:
    VideoDeviceRuntime(runtime::DeviceId device,
                       std::shared_ptr<runtime::GpuRuntime> gpu);

    /// Lazily initialize GpuRuntime + the FFmpeg CUDA hwdevice.    Returns
    /// false (and sets `reason`) on any initialization failure — callers must
    /// treat that as FAIL_CLOSED.
    bool ensure_initialized(std::string& reason);

    runtime::DeviceId device_;
    std::shared_ptr<runtime::GpuRuntime> gpu_;
    AVBufferRef* cuda_hwdevice_{nullptr};
    bool initialized_{false};
    bool init_ok_{false};
    std::mutex mutex_;
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
        std::shared_ptr<runtime::GpuRuntime> gpu = nullptr);

    [[nodiscard]] std::size_t size() const noexcept;

private:
    mutable std::mutex mutex_;
    std::unordered_map<runtime::DeviceId,
        std::shared_ptr<VideoDeviceRuntime>> runtimes_;
    // Optional engine-owned GPU runtimes, one per device. Keeping these here
    // makes the registry the single owner boundary for the video runtime.
    std::unordered_map<runtime::DeviceId,
        std::shared_ptr<runtime::GpuRuntime>> gpu_runtimes_;
};

} // namespace chronon3d::media