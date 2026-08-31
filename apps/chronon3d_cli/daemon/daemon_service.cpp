#include "daemon_service.hpp"

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#endif
#include "../utils/job/cli_render_utils.hpp"

#include "utils/common/render_error_formatter.hpp"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <array>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

namespace chronon3d::cli {

// ── Helpers ────────────────────────────────────────────────────────────────

namespace {

    graph::BackendPreference backend_preference_from_name(const std::string& value) {
        if (value == "software") return graph::BackendPreference::Software;
        if (value == "vulkan") return graph::BackendPreference::GPU;
        return graph::BackendPreference::Auto;
    }

    std::vector<std::string> split_args(const std::string& line) {
        std::vector<std::string> args;
        std::istringstream iss(line);
        std::string token;
        while (iss >> token) args.push_back(token);
        return args;
    }

    std::string format_output_path(const std::string& pattern, i32 frame) {
        std::string result = pattern;

        // Replace "####" with zero-padded 4-digit frame number.
        auto pos = result.find("####");
        if (pos != std::string::npos) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%04d", frame);
            result.replace(pos, 4, buf);
        }

        // Replace "#" with raw frame number.
        pos = result.find('#');
        if (pos != std::string::npos) {
            result.replace(pos, 1, std::to_string(frame));
        }

        return result;
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
        // Codec registration is not a capability claim: the concrete FFmpeg
        // codec must be present in this build before it is advertised.
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
        // These formats are advertised only for a CUDA-backed device with a
        // real NVENC/NVDEC path; Vulkan enumeration alone is insufficient.
        result.nv12 = result.cuda && (result.nvenc || result.nvdec);
        result.p010 = result.cuda && (result.nvenc || result.nvdec);
#endif
        return result;
    }
#endif

} // anonymous namespace

// ── Construction ──────────────────────────────────────────────────────────────

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

    // Do not construct a SoftwareRenderer at daemon startup.  Direct-YUV
    // video jobs do not use RenderEngine/SoftwareRenderer at all; creating a
    // warm renderer here would reintroduce the exact startup baggage that the
    // direct path removed.  FullGraph obtains a renderer lazily in
    // warm_renderer_for_device() on its first job.

    // Register the persistent daemon lane with the canonical scheduler.
    // Physical-device discovery remains owned by the backend; this service
    // advertises the selected Vulkan lane and keeps software fail-closed for
    // native GPU requests.
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

// ── Main Loop ─────────────────────────────────────────────────────────────────

void DaemonService::run() {
    spdlog::info("");
    spdlog::info("╔══════════════════════════════════════════╗");
    spdlog::info("║   🔥 Chronon3d Daemon Mode — Ready      ║");
    spdlog::info("╠══════════════════════════════════════════╣");
    spdlog::info("║  r  <comp> <frame> [out]     Render     ║");
    spdlog::info("║  st                          Stats      ║");
    spdlog::info("║  cc                          Clear cache║");
    spdlog::info("║  rl                          Rebuild    ║");
    spdlog::info("║  h                           Help       ║");
    spdlog::info("║  q                           Quit       ║");
    spdlog::info("╚══════════════════════════════════════════╝");
    spdlog::info("");

    std::string line;
    while (m_running && std::getline(std::cin, line)) {
        // Trim leading / trailing whitespace.
        while (!line.empty() && std::isspace(line.front()))
            line.erase(0, 1);
        while (!line.empty() && std::isspace(line.back()))
            line.pop_back();

        if (line.empty()) continue;
        handle_command(line);
    }

    spdlog::info("");
    spdlog::info("Daemon shutting down. {} frames rendered in {:.1f}ms total.",
                 m_render_count, m_total_render_ms);
}

// ── Command Dispatch ──────────────────────────────────────────────────────────

void DaemonService::handle_command(const std::string& line) {
    auto args = split_args(line);
    if (args.empty()) return;

    const auto& cmd = args[0];

    if (cmd == "render" || cmd == "r") {
        args.erase(args.begin());
        cmd_render(args);
    } else if (cmd == "reload" || cmd == "rl") {
        cmd_reload();
    } else if (cmd == "clear" || cmd == "cc") {
        cmd_clear_caches();
    } else if (cmd == "status" || cmd == "st") {
        cmd_status();
    } else if (cmd == "help" || cmd == "h") {
        cmd_help();
    } else if (cmd == "quit" || cmd == "q" || cmd == "exit") {
        m_running = false;
    } else {
        spdlog::warn("Unknown command: '{}'.  Type 'h' for help.", cmd);
    }
}

// ── Commands ──────────────────────────────────────────────────────────────────

void DaemonService::cmd_render(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        spdlog::error("Usage: r <comp_id> <frame> [output]");
        return;
    }

    const auto& comp_id = args[0];
    const Frame frame(std::stoi(args[1]));
    std::string output = args.size() > 2
                             ? args[2]
                             : "output/daemon_####.png";
    output = format_output_path(output, static_cast<i32>(frame));

    if (!m_registry.contains(comp_id)) {
        print_render_error(
            graph::NodeExecutionError{
                graph::RenderBackendErrorCode::InvalidInput,
                "composition_registry",
                "unknown composition '" + comp_id + "'"
            },
            comp_id,
            frame);
        return;
    }

    auto comp = m_registry.create(comp_id);
    auto compiled = chronon3d::compile_composition(
        comp, CompositionCompileContext{});
    if (!compiled) {
        spdlog::error("Failed to compile composition '{}': {}",
                      comp_id, compiled.error().message);
        return;
    }

    const auto t0 = profiling::now();
    // Runtime execution accepts only the immutable compiled composition.
    auto fb = m_engine->render_compiled(
        std::move(compiled).value(), frame);
    const auto t1 = profiling::now();

    if (!fb) {
        const auto structured = m_engine->last_render_error();
        if (structured) {
            print_render_error(*structured, comp_id, frame);
        } else {
            print_render_error(
                graph::NodeExecutionError{
                    graph::RenderBackendErrorCode::ExecutionFailure,
                    "render",
                    "renderer returned a null framebuffer without a structured node error"
                },
                comp_id,
                frame);
        }
        return;
    }

    const double render_ms = profiling::duration_ms(t0, t1);

    // ── Save to disk ─────────────────────────────────────────────────
    std::filesystem::path out_path(output);
    if (out_path.has_parent_path()) {
        std::filesystem::create_directories(out_path.parent_path());
    }

    const auto encode_t0 = profiling::now();
    ImageWriteOptions write_opts;
    write_opts.format = image_format_from_path(output);
    if (!save_image(*fb, output, write_opts)) {
        spdlog::error("Failed to save frame to '{}'", output);
        return;
    }
    const double encode_ms = profiling::duration_ms(encode_t0, profiling::now());

    m_render_count++;
    m_total_render_ms += render_ms;

    spdlog::info("✅ {} f{} → {}  |  render {:.1f}ms  encode {:.1f}ms",
                 comp_id, frame, output, render_ms, encode_ms);
}

void DaemonService::cmd_reload() {
    if (m_options.build_command.empty()) {
        spdlog::warn("No build command configured.  Skipping reload.");
        return;
    }

    spdlog::info("🔨 Building: {}", m_options.build_command);
    const auto t0 = profiling::now();
    int ret = std::system(m_options.build_command.c_str());
    const auto t1 = profiling::now();

    if (ret != 0) {
        spdlog::error("Build failed (exit code {}).  Engine state preserved.",
                      ret);
        return;
    }

    spdlog::info("✅ Build OK ({:.1f}s).  New binary built.  Use "
                 "`chronon watch <comp>` for hot-reload, or restart this "
                 "daemon to pick up the new binary.",
                 profiling::duration_ms(t0, t1) / 1000.0);
}

void DaemonService::cmd_clear_caches() {
    const auto t0 = profiling::now();
    m_engine->clear_caches();
    m_engine->reset_counters();
    const auto t1 = profiling::now();

    spdlog::info("🧹 All caches cleared in {:.1f}ms",
                 profiling::duration_ms(t0, t1));
}

void DaemonService::cmd_status() {
    // P1-F Pass D — `engine->renderer()->counters()` was the only OPP-internal
    // escape hatch that ran through RenderEngineAccess.  Counters are no
    // longer exposed on the public SDK surface (the V0.1 facade keeps that
    // internal).  The daemon-side status block drops the counters panel but
    // keeps the per-session tallies (`m_render_count`, `m_total_render_ms`)
    // which are still visible and useful.

    spdlog::info("");
    spdlog::info("═══ Daemon Status ═══");
    spdlog::info("  Frames rendered : {}", m_render_count);
    spdlog::info("  Total render ms : {:.1f}", m_total_render_ms);
    if (m_render_count > 0) {
        spdlog::info("  Avg render ms   : {:.1f}",
                     m_total_render_ms / m_render_count);
    }
    spdlog::info("");
}

void DaemonService::cmd_help() {
    spdlog::info("");
    spdlog::info("Commands (short aliases in parentheses):");
    spdlog::info("  r      <comp> <frame> [out]   Render a single frame");
    spdlog::info("  st                             Show engine statistics");
    spdlog::info("  cc                             Clear all caches (FB pool, fonts, nodes)");
    spdlog::info("  rl                             Rebuild project (requires build command)");
    spdlog::info("  h                              Show this help");
    spdlog::info("  q                              Shutdown daemon");
    spdlog::info("");
}

// ── UNIX-socket IPC (RenderingGen → Chronon) ───────────────────────────────

RenderJobDispatcher& render_job_dispatcher() {
    static RenderJobDispatcher dispatcher;
    return dispatcher;
}

WarmRenderJobDispatcher& warm_render_job_dispatcher() {
    static WarmRenderJobDispatcher dispatcher;
    return dispatcher;
}

void DaemonService::run_socket(const std::string& path) {
    ipc::UnixSocketServer server;
    try {
        server.listen(path);
    } catch (const std::system_error& e) {
        spdlog::error("Failed to bind Unix socket '{}': {}", path, e.what());
        return;
    }

    spdlog::info("🔌 Daemon listening on Unix socket '{}'", server.path());
    spdlog::info("   PREFETCH_ASSET | PREPARE_PLAN | RENDER_OVERLAY | RENDER_JOB | STATUS | SHUTDOWN");

    const int rc = server.serve_concurrent([this](const ipc::Request& req) {
        return handle_ipc(req);
    });

    if (rc != 0) {
        spdlog::error("Unix socket serve loop exited with error {} ({})",
                      rc, std::strerror(rc));
    }
    spdlog::info("Daemon socket shutdown. {} frames rendered in {:.1f}ms total.",
                 m_render_count, m_total_render_ms);
}

ipc::Reply DaemonService::handle_ipc(const ipc::Request& req) {
    // Socket ingress is concurrent, but the warm engine/prepared plan is a
    // single session. DeviceSessionPool will replace this serialized lane
    // when per-device workers are available; until then this lock is the
    // ownership boundary that prevents cross-request renderer races.
    switch (req.cmd) {
        case ipc::Command::PrefetchAsset: {
            std::lock_guard<std::mutex> lock(m_ipc_state_mutex);
            return ipc_prefetch_asset(req.payload);
        }
        case ipc::Command::PreparePlan: {
            std::lock_guard<std::mutex> lock(m_ipc_state_mutex);
            return ipc_prepare_plan(req.payload);
        }
        case ipc::Command::RenderOverlay: {
            std::lock_guard<std::mutex> lock(m_ipc_state_mutex);
            return ipc_render_overlay(req.payload);
        }
        case ipc::Command::RenderJob: {
            // A daemon owns one persistent renderer. Reject a request that
            // claims a different backend instead of silently rendering on the
            // warm backend and producing misleading provenance.
            nlohmann::json request;
            try {
                request = nlohmann::json::parse(req.payload);
                const auto requested = request.value("backend", m_backend);
                if (requested != m_backend &&
                    !(requested == "auto" && m_backend == "auto")) {
                    return ipc::Reply{ipc::Status::BadRequest,
                                      "RENDER_JOB backend '" + requested +
                                      "' does not match daemon backend '" +
                                      m_backend + "'"};
                }
            } catch (const std::exception& e) {
                return ipc::Reply{ipc::Status::BadRequest,
                                  std::string{"RENDER_JOB parse failed: "} + e.what()};
            }
            const auto requested_backend = request.value("backend", m_backend);
            // New IPC clients describe requirements, not Chronon's backend
            // implementation. Keep the legacy fields as a compatibility
            // fallback for older workers and direct CLI integrations.
            const bool semantic_contract = request.contains("execution_requirements");
            const auto semantic_requirements = request.value(
                "execution_requirements", nlohmann::json::object());
            const bool gpu_required = semantic_requirements.value("gpu_required", false);
            const bool composition_required = semantic_requirements.value(
                "composition_required", true);
            const std::string hardware = semantic_contract
                ? (gpu_required ? "nvenc" : "none")
                : request.value("hardware_encoder",
                    request.value("hardware", std::string{"none"}));
            const std::string encoder_backend = semantic_contract
                ? (gpu_required ? "native" : "pipe")
                : request.value("encoder_backend", std::string{"pipe"});
            const auto output_spec = request.value(
                "output_spec", nlohmann::json::object());
            const std::string codec = semantic_contract
                ? output_spec.value("codec", std::string{"auto"})
                : request.value("codec", std::string{"auto"});
            const std::string hot_path = semantic_contract
                ? (gpu_required
                    ? (composition_required ? "require_gpu_native" : "require_direct_yuv")
                    : "auto")
                : request.value("gpu_hot_path_mode", std::string{"auto"});
            const bool native_nvenc =
                (requested_backend == "vulkan" ||
                 (requested_backend == "auto" && m_backend == "vulkan")) &&
                (hardware == "nvenc" || hardware == "auto") &&
                encoder_backend == "native";
            runtime::DeviceSelectionRequirements requirements;
            // A render is a share of the GPU, not an exclusive whole-device
            // lock. NVENC/NVDEC session counts remain hard limits; compute
            // pressure at 0.5 allows two independent jobs when those engines
            // have capacity instead of serialising every export at 1.0.
            requirements.resources.compute_units = native_nvenc ? 0.5f : 0.0f;
            requirements.cuda = native_nvenc;
            requirements.vulkan_interop = native_nvenc &&
                hot_path != "require_direct_yuv";
            requirements.nvenc = native_nvenc;
            requirements.nv12 = native_nvenc;
            requirements.resources.nvenc_sessions = native_nvenc ? 1U : 0U;
            requirements.resources.vram_bytes = native_nvenc
                ? 512ULL * 1024ULL * 1024ULL : 0;
            requirements.resources.pcie_bandwidth = native_nvenc ? 0.25f : 0.0f;
            requirements.h264 = native_nvenc &&
                (codec == "auto" || codec == "h264" || codec == "libx264" ||
                 codec == "h264_nvenc");
            requirements.hevc = native_nvenc &&
                (codec == "hevc" || codec == "h265" || codec == "libx265" ||
                 codec == "hevc_nvenc");
            requirements.av1 = native_nvenc &&
                (codec == "av1" || codec == "libsvtav1" || codec == "av1_nvenc");
            auto reservation = m_device_scheduler.reserve(requirements);
            if (!reservation) {
                return ipc::Reply{ipc::Status::Error,
                                  "RENDER_JOB rejected: no device satisfies requested capabilities"};
            }
            const auto reserved_device = reservation->device();
            spdlog::debug("[daemon] RENDER_JOB placed on device {} (native_nvenc={})",
                          reserved_device, native_nvenc);
            auto video_execution =
                std::make_shared<media::VideoJobExecutionContext>();
            video_execution->device_id = reserved_device;
            if (const auto caps = m_device_scheduler.capability_snapshot(reserved_device)) {
                video_execution->cuda_device_ordinal = caps->cuda_device_ordinal;
                video_execution->physical_device_index = caps->physical_device_index;
            }
            video_execution->reservation = std::move(reservation);
            video_execution->video_runtimes = m_video_runtimes;
            // Direct-YUV owns its CUDA video runtime and deliberately must not
            // receive a SoftwareRenderer.  This keeps the worker lightweight
            // for the common native video lane while preserving a warm
            // renderer for FullGraph jobs.
            const bool direct_yuv = native_nvenc && hot_path == "require_direct_yuv";
            std::lock_guard<std::mutex> lock(m_ipc_state_mutex);
            std::shared_ptr<SoftwareRenderer> warm_renderer;
            if (!direct_yuv) {
                warm_renderer = warm_renderer_for_device(reserved_device);
            } else {
                spdlog::debug("[daemon] Direct-YUV job: renderer construction skipped");
            }
            auto& warm_dispatcher = warm_render_job_dispatcher();
            if (warm_dispatcher) {
                return warm_dispatcher(req.payload, std::move(warm_renderer),
                                       std::move(video_execution));
            }
            auto& dispatcher = render_job_dispatcher();
            if (dispatcher) {
                return dispatcher(req.payload, std::move(video_execution));
            }
            return ipc::Reply{ipc::Status::NotFound,
                              "RENDER_JOB unavailable: render group not compiled"};
        }
        case ipc::Command::Status: {
            std::lock_guard<std::mutex> lock(m_ipc_state_mutex);
            return ipc_status();
        }
        case ipc::Command::Shutdown:
            return ipc::Reply{ipc::Status::Shutdown, "bye"};
        default:
            return ipc::Reply{ipc::Status::BadRequest, "unknown command"};
    }
}

ipc::Reply DaemonService::ipc_prefetch_asset(const std::string& path) {
    if (path.empty()) {
        return ipc::Reply{ipc::Status::BadRequest, "PREFETCH_ASSET requires an asset path"};
    }
    // Register the asset in the engine-owned registry so subsequent renders
    // resolve it from the warm cache instead of re-importing it.
    const auto id = m_engine->assets().import_by_extension(path);
    if (!id) {
        return ipc::Reply{ipc::Status::NotFound,
                          "unrecognized asset extension: '" + path + "'"};
    }
    spdlog::info("📦 PREFETCH_ASSET {} (id {})", path, *id);
    return ipc::Reply{ipc::Status::Ok, "prefetched"};
}

ipc::Reply DaemonService::ipc_prepare_plan(const std::string& comp_id) {
    if (comp_id.empty()) {
        return ipc::Reply{ipc::Status::BadRequest, "PREPARE_PLAN requires a composition id"};
    }
    if (!m_registry.contains(comp_id)) {
        return ipc::Reply{ipc::Status::NotFound, "unknown composition '" + comp_id + "'"};
    }
    try {
        auto comp = m_registry.create(comp_id);
        m_prepared_job =
            std::make_unique<PreparedRenderJob>(m_engine->prepare(comp));
        m_prepared_comp_id = comp_id;
    } catch (const std::exception& e) {
        m_prepared_job.reset();
        m_prepared_comp_id.clear();
        return ipc::Reply{ipc::Status::Error, std::string{"prepare failed: "} + e.what()};
    }
    spdlog::info("🧠 PREPARE_PLAN '{}' — resource plan slots: {}",
                 comp_id, m_prepared_job->resource_plan().slots.size());
    return ipc::Reply{ipc::Status::Ok, "plan ready"};
}

ipc::Reply DaemonService::ipc_render_overlay(const std::string& args) {
    const auto tokens = split_args(args);
    if (tokens.empty()) {
        return ipc::Reply{ipc::Status::BadRequest,
                          "RENDER_OVERLAY requires '<frame> [output]'"};
    }
    if (!m_prepared_job) {
        return ipc::Reply{ipc::Status::Error,
                          "no prepared plan — send PREPARE_PLAN first"};
    }

    Frame frame{0};
    try {
        frame = Frame(std::stoll(tokens[0]));
    } catch (const std::exception&) {
        return ipc::Reply{ipc::Status::BadRequest,
                          "invalid frame number: '" + tokens[0] + "'"};
    }

    std::string output = tokens.size() > 1 ? tokens[1]
                                           : "output/overlay_####.png";
    output = format_output_path(output, static_cast<i32>(frame));

    std::shared_ptr<Framebuffer> fb;
    try {
        fb = m_prepared_job->render(frame);
    } catch (const std::exception& e) {
        return ipc::Reply{ipc::Status::Error,
                          std::string{"render failed: "} + e.what()};
    }
    if (!fb) {
        return ipc::Reply{ipc::Status::Error,
                          "renderer returned a null framebuffer"};
    }

    std::filesystem::path out_path(output);
    if (out_path.has_parent_path()) {
        std::filesystem::create_directories(out_path.parent_path());
    }
    ImageWriteOptions write_opts;
    write_opts.format = image_format_from_path(output);
    if (!save_image(*fb, output, write_opts)) {
        return ipc::Reply{ipc::Status::Error, "failed to save frame to '" + output + "'"};
    }

    m_render_count++;
    return ipc::Reply{ipc::Status::Ok, output};
}

ipc::Reply DaemonService::ipc_status() {
    std::ostringstream ss;
    ss << "frames_rendered=" << m_render_count
       << " total_ms=" << m_total_render_ms
       << " prepared_comp=" << (m_prepared_comp_id.empty() ? "(none)" : m_prepared_comp_id);
    return ipc::Reply{ipc::Status::Ok, ss.str()};
}

} // namespace chronon3d::cli
