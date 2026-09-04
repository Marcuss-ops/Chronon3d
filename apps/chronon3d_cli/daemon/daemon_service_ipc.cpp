#include "daemon_service.hpp"
#include "daemon_service_internal.hpp"

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>
#include <chronon3d/media/video/packet_assembler.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstring>
#include <filesystem>
#include <sstream>
#include <system_error>

namespace chronon3d::cli {

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
            media::ExecutionRequirements exec_reqs;
            if (request.contains("execution_requirements")) {
                const auto& sr = request["execution_requirements"];
                exec_reqs.gpu_required = sr.value("gpu_required", false);
                exec_reqs.cpu_fallback_allowed = sr.value("cpu_fallback_allowed", true);
                exec_reqs.composition_required = sr.value("composition_required", true);
                exec_reqs.packet_copy_allowed = sr.value("packet_copy_allowed", true);
            } else {
                const std::string hw = request.value("hardware_encoder",
                    request.value("hardware", std::string{"none"}));
                exec_reqs.gpu_required = (hw == "nvenc");
            }
            const std::string requested_codec = request.contains("output_spec")
                ? request["output_spec"].value("codec", "")
                : request.value("codec", "");

            const auto canon = media::resolve_canonical_execution_parameters(
                exec_reqs, requested_codec, requested_backend, m_backend);
            const bool native_nvenc = canon.native_nvenc;
            const std::string hot_path = canon.gpu_hot_path_mode;
            const std::string codec = canon.codec;
            runtime::DeviceSelectionRequirements requirements;
            requirements.resources.compute_units = native_nvenc ? 0.5f : 0.0f;
            requirements.cuda = native_nvenc;
            requirements.vulkan_interop = native_nvenc && hot_path != "require_direct_yuv";
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
            auto video_execution = std::make_shared<media::VideoJobExecutionContext>();
            video_execution->device_id = reserved_device;
            if (const auto caps = m_device_scheduler.capability_snapshot(reserved_device)) {
                video_execution->cuda_device_ordinal = caps->cuda_device_ordinal;
                video_execution->physical_device_index = caps->physical_device_index;
            }
            video_execution->reservation = std::move(reservation);
            video_execution->video_runtimes = m_video_runtimes;
            const bool direct_yuv = canon.direct_yuv;
            std::lock_guard<std::mutex> lock(m_ipc_state_mutex);
            std::shared_ptr<SoftwareRenderer> warm_renderer;
            if (!direct_yuv) {
                warm_renderer = warm_renderer_for_device(reserved_device);
            } else {
                spdlog::debug("[daemon] Direct-YUV job: renderer construction skipped");
            }
            request["backend"] = canon.backend;
            request["hardware_encoder"] = canon.hardware_encoder;
            request["encoder_backend"] = canon.encoder_backend;
            request["codec"] = canon.codec;
            request["gpu_hot_path_mode"] = canon.gpu_hot_path_mode;
            const std::string forward_payload = request.dump();

            auto& warm_dispatcher = warm_render_job_dispatcher();
            if (warm_dispatcher) {
                return warm_dispatcher(forward_payload, std::move(warm_renderer),
                                       std::move(video_execution));
            }
            auto& dispatcher = render_job_dispatcher();
            if (dispatcher) {
                return dispatcher(forward_payload, std::move(video_execution));
            }
            return ipc::Reply{ipc::Status::NotFound,
                              "RENDER_JOB unavailable: render group not compiled"};
        }
        case ipc::Command::AssembleSegments:
            return ipc_assemble_segments(req.payload);
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
        m_prepared_job = std::make_unique<PreparedRenderJob>(m_engine->prepare(comp));
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
    const auto tokens = daemon_detail::split_args(args);
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

    std::string output = tokens.size() > 1 ? tokens[1] : "output/overlay_####.png";
    output = daemon_detail::format_output_path(output, static_cast<std::int32_t>(frame));

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

ipc::Reply DaemonService::ipc_assemble_segments(const std::string& payload) {
#if defined(CHRONON3D_ENABLE_VIDEO)
    try {
        const auto request = nlohmann::json::parse(payload);
        const auto paths = request.value("input_paths", std::vector<std::string>{});
        const auto output = request.value("output_path", std::string{});
        std::string joined;
        for (const auto& path : paths) { if (!joined.empty()) joined += ", "; joined += path; }
        const auto result = media::assemble_segments(media::SegmentAssemblyRequest{paths, output});
        if (!result.success) return ipc::Reply{ipc::Status::Error, result.reason};
        return ipc::Reply{ipc::Status::Ok, output};
    } catch (const std::exception& e) {
        return ipc::Reply{ipc::Status::BadRequest, std::string{"ASSEMBLE_SEGMENTS parse failed: "} + e.what()};
    }
#else
    (void)payload;
    return ipc::Reply{ipc::Status::Error,
                      std::string{"ASSEMBLE_SEGMENTS unavailable: built without CHRONON3D_ENABLE_VIDEO"}};
#endif
}

ipc::Reply DaemonService::ipc_status() {
    std::ostringstream ss;
    ss << "frames_rendered=" << m_render_count
       << " total_ms=" << m_total_render_ms
       << " prepared_comp=" << (m_prepared_comp_id.empty() ? "(none)" : m_prepared_comp_id);
    return ipc::Reply{ipc::Status::Ok, ss.str()};
}

} // namespace chronon3d::cli
