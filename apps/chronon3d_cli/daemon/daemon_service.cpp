#include "daemon_service.hpp"
#include "daemon_service_internal.hpp"

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>
#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#endif
#include "../utils/job/cli_render_utils.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <sstream>
#include <utility>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

namespace chronon3d::cli {

namespace daemon_detail {

std::vector<std::string> split_args(const std::string& line) {
    std::vector<std::string> args;
    std::istringstream iss(line);
    std::string token;
    while (iss >> token) args.push_back(token);
    return args;
}

std::string format_output_path(const std::string& pattern, std::int32_t frame) {
    std::string result = pattern;
    auto pos = result.find("####");
    if (pos != std::string::npos) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d", frame);
        result.replace(pos, 4, buf);
    }
    pos = result.find('#');
    if (pos != std::string::npos) {
        result.replace(pos, 1, std::to_string(frame));
    }
    return result;
}

} // namespace daemon_detail

namespace {

graph::BackendPreference backend_preference_from_name(const std::string& value) {
    if (value == "software") return graph::BackendPreference::Software;
    if (value == "vulkan") return graph::BackendPreference::GPU;
    return graph::BackendPreference::Auto;
}

#ifdef CHRONON3D_ENABLE_VULKAN
struct ProbedVideoCapabilities {
    bool cuda{false};
    bool nvdec{false};
    bool nvenc{false};
    bool nv12{false};
    bool p010{false};
    bool h264{false};
    bool hevc{false};
    bool av1{false};
    std::int32_t cuda_ordinal{-1};
};

ProbedVideoCapabilities probe_video_capabilities(
    const backends::vulkan::VulkanDeviceInfo& device) {
    ProbedVideoCapabilities result;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    if (cuInit(0) != CUDA_SUCCESS || !device.has_device_uuid) return result;
    int count = 0;
    if (cuDeviceGetCount(&count) != CUDA_SUCCESS) return result;
    for (int ordinal = 0; ordinal < count; ++ordinal) {
        CUdevice cuda_device{};
        CUuuid cuda_uuid{};
        if (cuDeviceGet(&cuda_device, ordinal) != CUDA_SUCCESS ||
            cuDeviceGetUuid(&cuda_uuid, cuda_device) != CUDA_SUCCESS) continue;
        if (std::equal(device.device_uuid.begin(), device.device_uuid.end(),
                       reinterpret_cast<const std::uint8_t*>(cuda_uuid.bytes))) {
            result.cuda = true;
            result.cuda_ordinal = ordinal;
            break;
        }
    }
#endif
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    const auto has_encoder = [](const char* name) {
        return avcodec_find_encoder_by_name(name) != nullptr;
    };
    const auto has_decoder = [](const char* name) {
        return avcodec_find_decoder_by_name(name) != nullptr;
    };
    result.h264 = has_encoder("h264_nvenc") || has_decoder("h264_cuvid");
    result.hevc = has_encoder("hevc_nvenc") || has_decoder("hevc_cuvid");
    result.av1 = has_encoder("av1_nvenc") || has_decoder("av1_cuvid");
    result.nvenc = has_encoder("h264_nvenc") || has_encoder("hevc_nvenc") ||
                  has_encoder("av1_nvenc");
    result.nvdec = has_decoder("h264_cuvid") || has_decoder("hevc_cuvid") ||
                   has_decoder("av1_cuvid");
    result.nv12 = result.cuda && (result.nvenc || result.nvdec);
    result.p010 = result.cuda && (result.nvenc || result.nvdec);
#endif
    return result;
}
#endif

} // anonymous namespace

DaemonService::DaemonService(const CompositionRegistry& registry,
                             DaemonOptions options)
    : m_registry(registry)
    , m_options(std::move(options))
    , m_backend(m_options.backend.empty() ? "auto" : m_options.backend)
    , m_video_runtimes(std::make_shared<media::VideoRuntimeRegistry>())
{
#ifdef CHRONON3D_ENABLE_VULKAN
    std::vector<backends::vulkan::VulkanDeviceInfo> discovered_devices;
    if (m_backend == "vulkan" || m_backend == "auto") {
        discovered_devices = backends::vulkan::VulkanBackend::enumerate_devices();
        if (m_options.gpu_device_id == Config::kAutoGpuDevice &&
            !discovered_devices.empty()) {
            const auto selected = std::find_if(
                discovered_devices.begin(), discovered_devices.end(),
                [](const auto& device) { return device.discrete; });
            m_options.gpu_device_id = selected == discovered_devices.end()
                ? discovered_devices.front().index : selected->index;
        }
    }
#else
    std::vector<int> discovered_devices;
#endif
    Config config = Config::from_environment();
    config.set_backend_preference(backend_preference_from_name(m_backend));
    config.set_gpu_device_id(m_options.gpu_device_id);

    if (!m_options.assets_root.empty()) {
        m_engine = std::make_unique<RenderEngine>(
            std::move(config), m_options.assets_root);
    } else {
        m_engine = std::make_unique<RenderEngine>(std::move(config));
    }

    m_engine->set_composition_registry(&m_registry);

#ifdef CHRONON3D_ENABLE_VULKAN
    const auto register_device = [this](const backends::vulkan::VulkanDeviceInfo& device) {
        const auto probed = probe_video_capabilities(device);
        runtime::DeviceCapabilities capabilities;
        capabilities.physical_device_index = device.index;
        capabilities.name = device.name;
        capabilities.uuid = device.device_uuid;
        capabilities.has_uuid = device.has_device_uuid;
        capabilities.cuda_device_ordinal = probed.cuda_ordinal;
        capabilities.cuda = probed.cuda;
        capabilities.vulkan_interop = device.has_device_uuid && probed.cuda;
        capabilities.nvdec = probed.nvdec && probed.cuda;
        capabilities.nvenc = probed.nvenc && probed.cuda;
        capabilities.nv12 = probed.nv12;
        capabilities.p010 = probed.p010;
        capabilities.h264 = probed.h264 && probed.cuda;
        capabilities.hevc = probed.hevc && probed.cuda;
        capabilities.av1 = probed.av1 && probed.cuda;
        m_device_scheduler.register_device(
            std::move(capabilities),
            runtime::DeviceResourceVector{
                .compute_units = 1.0f,
                .vram_bytes = device.device_memory_bytes,
                .nvdec_sessions = probed.nvdec ? 2U : 0U,
                .nvenc_sessions = probed.nvenc ? 2U : 0U,
                .pcie_bandwidth = 1.0f});
    };
    for (const auto& device : discovered_devices) {
        register_device(device);
    }
#endif
    if (m_device_scheduler.device_count() == 0) {
        runtime::DeviceCapabilities capabilities;
        capabilities.id = 0;
        capabilities.name = m_backend;
        m_device_scheduler.register_device(
            std::move(capabilities),
            runtime::DeviceResourceVector{.compute_units = 1.0f});
    }
    spdlog::info("🔥 Worker initialised. backend={}, renderer lazy, asset/CUDA caches process-persistent.",
                 m_backend);
    spdlog::info("   {} compositions registered.", m_registry.available().size());
}

DaemonService::~DaemonService() = default;

std::shared_ptr<SoftwareRenderer> DaemonService::warm_renderer_for_device(
    runtime::DeviceId device) {
    const auto existing = m_device_sessions.find(device);
    if (existing != m_device_sessions.end()) return existing->second;

    Config config = Config::from_environment();
    config.set_backend_preference(backend_preference_from_name(m_backend));
    const auto capabilities = m_device_scheduler.capability_snapshot(device);
    config.set_gpu_device_id(capabilities ? capabilities->physical_device_index : device);
    auto renderer = create_renderer(
        m_registry, RenderSettings{}, std::move(config),
        m_options.assets_root.empty()
            ? std::optional<std::filesystem::path>{}
            : std::optional<std::filesystem::path>{m_options.assets_root});
    m_device_sessions.emplace(device, renderer);
    spdlog::info("[daemon] created warm device session {}", device);
    return renderer;
}

} // namespace chronon3d::cli
