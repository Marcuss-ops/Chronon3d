#include "video_export_native_support.hpp"

#include "../../../daemon/daemon_render_concurrency.hpp"
#include "../../../utils/video/encoder_config_resolution.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cstdint>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif

namespace chronon3d::cli {

bool configure_local_device_scheduler(
    const FfmpegExportOptions& opts,
    runtime::DeviceScheduler& local_scheduler,
    runtime::DeviceScheduler*& active_scheduler)
{
    runtime::DeviceCapabilities capabilities;
    std::uint64_t device_vram_bytes = 0;
    capabilities.id = 0;
    capabilities.name = "cli-device-0";
#if defined(CHRONON3D_ENABLE_CUDA_INTEROP) && defined(CHRONON3D_ENABLE_VULKAN)
    bool has_cuda = (cuInit(0) == CUDA_SUCCESS);
    int cuda_count = 0;
    if (has_cuda && cuDeviceGetCount(&cuda_count) == CUDA_SUCCESS && cuda_count > 0) {
        capabilities.cuda = true;
        // P0-2/P0-4: the local admission table must describe the physical
        // device. It used to publish `vram_bytes = 0`, which disables the
        // DeviceScheduler's VRAM gate entirely (`capacity.vram_bytes > 0`),
        // so a CLI job could be admitted against a working set the card
        // cannot hold. The estimate is now ~8.8 GB for 1080p FullGraph, and
        // this capacity is what makes that estimate actually gate.
        CUdevice cuda_device{};
        if (cuDeviceGet(&cuda_device, 0) == CUDA_SUCCESS) {
            std::size_t total_bytes = 0;
            if (cuDeviceTotalMem(&total_bytes, cuda_device) == CUDA_SUCCESS) {
                device_vram_bytes = static_cast<std::uint64_t>(total_bytes);
            }
        }
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
        const auto has_encoder = [](const char* name) {
            return avcodec_find_encoder_by_name(name) != nullptr;
        };
        const auto has_decoder = [](const char* name) {
            return avcodec_find_decoder_by_name(name) != nullptr;
        };
        capabilities.h264 = has_encoder("h264_nvenc") || has_decoder("h264_cuvid");
        capabilities.hevc = has_encoder("hevc_nvenc") || has_decoder("hevc_cuvid");
        capabilities.av1 = has_encoder("av1_nvenc") || has_decoder("av1_cuvid");
        capabilities.nvenc = has_encoder("h264_nvenc") || has_encoder("hevc_nvenc") || has_encoder("av1_nvenc");
        capabilities.nvdec = has_decoder("h264_cuvid") || has_decoder("hevc_cuvid") || has_decoder("av1_cuvid");
        capabilities.nv12 = capabilities.cuda && (capabilities.nvenc || capabilities.nvdec);
        capabilities.p010 = capabilities.cuda && (capabilities.nvenc || capabilities.nvdec);
#endif
    }
#endif
    if (hardware_nvenc_requested(opts.encoder.hardware_encoder) &&
        !capabilities.nvenc) {
        spdlog::error(
            "[video] hardware encoder 'nvenc' requested but probed device capabilities lack nvenc support");
        return false;
    }
    // Read the sessions before moving `capabilities` into the table, and
    // take the execution-domain slot count from the single concurrency
    // authority instead of hardcoding a second number here.
    const std::uint32_t nvdec_sessions = capabilities.nvdec ? 1u : 0u;
    const std::uint32_t nvenc_sessions = capabilities.nvenc ? 1u : 0u;
    const std::uint32_t render_job_slots =
        resolve_render_job_slots(std::getenv(kRenderJobSlotsEnvVar)).slots;
    if (device_vram_bytes == 0 && (capabilities.nvenc || capabilities.nvdec)) {
        spdlog::warn(
            "[video] CUDA device memory could not be probed; DeviceScheduler VRAM "
            "admission is disabled for this device");
    }
    local_scheduler.register_device(
        std::move(capabilities),
        runtime::DeviceResourceVector{
            .compute_units = 1.0f,
            .vram_bytes = device_vram_bytes,
            .nvdec_sessions = nvdec_sessions,
            .nvenc_sessions = nvenc_sessions,
            .pcie_bandwidth = 1.0f,
            .render_job_slots = render_job_slots});
    active_scheduler = &local_scheduler;
    return true;
}

} // namespace chronon3d::cli
