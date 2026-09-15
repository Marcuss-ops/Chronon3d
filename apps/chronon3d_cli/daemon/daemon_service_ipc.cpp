#include "daemon_service.hpp"
#include "daemon_render_concurrency.hpp"
#include "daemon_render_job_payload.hpp"
#include "daemon_service_internal.hpp"

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>
#include <chronon3d/media/video/packet_assembler.hpp>
#include <chronon3d/runtime/job/render_job_request.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace chronon3d::cli {

namespace {

// Read a string field preferring the `output_spec` object, then the payload
// root. Mirrors the historical daemon/render-group payload contract where a
// client may nest encoder/rate-control intent under output_spec or keep it
// flat (Phase 9: one canonical decode for both spellings).
std::string spec_or_root(
    const nlohmann::json& payload, const char* key) {
    const auto it = payload.find("output_spec");
    if (it != payload.end() && it->is_object() && it->contains(key) &&
        !(*it)[key].is_null()) {
        return (*it)[key].get<std::string>();
    }
    const auto root = payload.find(key);
    if (root != payload.end() && !root->is_null()) {
        return root->get<std::string>();
    }
    return {};
}

std::int64_t spec_or_root_int(
    const nlohmann::json& payload, const char* key, std::int64_t fallback) {
    const auto it = payload.find("output_spec");
    if (it != payload.end() && it->is_object() && it->contains(key) &&
        !(*it)[key].is_null()) {
        return (*it)[key].get<std::int64_t>();
    }
    const auto root = payload.find(key);
    if (root != payload.end() && !root->is_null()) {
        return root->get<std::int64_t>();
    }
    return fallback;
}

bool spec_or_root_bool(
    const nlohmann::json& payload, const char* key, bool fallback) {
    const auto it = payload.find("output_spec");
    if (it != payload.end() && it->is_object() && it->contains(key) &&
        !(*it)[key].is_null()) {
        return (*it)[key].get<bool>();
    }
    const auto root = payload.find(key);
    if (root != payload.end() && !root->is_null()) {
        return root->get<bool>();
    }
    return fallback;
}

void extract_plan_dimensions(const std::filesystem::path& plan_path,
                            std::uint32_t& width, std::uint32_t& height) {
    if (width > 0 && height > 0) return;
    if (plan_path.empty()) return;
    try {
        std::ifstream file(plan_path);
        if (!file.is_open()) return;
        nlohmann::json plan_json;
        file >> plan_json;
        if (plan_json.contains("canvas") && plan_json["canvas"].is_object()) {
            width = plan_json["canvas"].value("width", 1920U);
            height = plan_json["canvas"].value("height", 1080U);
        } else if (plan_json.contains("width") && plan_json.contains("height")) {
            width = plan_json.value("width", 1920U);
            height = plan_json.value("height", 1080U);
        }
    } catch (...) {}
}

std::uint64_t estimate_peak_vram_bytes(
    std::uint32_t width, std::uint32_t height, bool direct_yuv, bool native_nvenc) {
    if (!native_nvenc) return 0;
    if (width == 0) width = 1920;
    if (height == 0) height = 1080;
    if (direct_yuv) {
        return 384ULL * 1024ULL * 1024ULL; // 384 MB for direct YUV pipeline
    }
    // FullGraph Vulkan + CUDA + NVENC:
    // Frame execution slot surfaces: RGBA32F = width * height * 16 bytes.
    // 8x dynamic buffer multiplier covers ring depth (4), staging surfaces,
    // and intermediate scratch targets. Base 512 MB covers driver, Vulkan device,
    // pipeline state and NVENC sessions.
    const uint64_t frame_bytes = static_cast<uint64_t>(width) * height * 16ULL;
    const uint64_t dynamic_bytes = 8ULL * frame_bytes;
    const uint64_t estimated = (512ULL * 1024ULL * 1024ULL) + dynamic_bytes;
    return std::max<uint64_t>(estimated, 1024ULL * 1024ULL * 1024ULL); // Minimum 1 GB
}

} // namespace

RenderJobDispatcher& render_job_dispatcher() {
    static RenderJobDispatcher dispatcher;
    return dispatcher;
}

runtime::job::RenderJobRequest decode_render_job_payload(
    const std::string& payload) {
    const auto request = nlohmann::json::parse(payload);

    runtime::job::RenderJobRequest job_request;
    job_request.plan_path = request.value("plan_path", "");
    job_request.prepared_package_path = request.value("prepared_package_path", "");
    job_request.output_path = request.value("output", "");
    job_request.assets_root = request.value("assets_root", "");
    job_request.report = request.value("report", false);
    job_request.trace.output = request.value("trace_output", "");
    job_request.trace.level = request.value("trace_level", "pipeline");
    job_request.receipt_verification = request.value("receipt_verify", "");

    const bool range_enabled = request.value("range_enabled", false);
    if (range_enabled) {
        job_request.frame_range = runtime::job::FrameRange{
            request.value("first_frame", std::int64_t{0}),
            request.value("last_frame", std::int64_t{0})};
    }

    auto& out = job_request.output;
    out.codec = spec_or_root(request, "codec");
    out.hardware_encoder = spec_or_root(request, "hardware_encoder");
    if (out.hardware_encoder.empty()) {
        // Legacy alias accepted by the pre-Phase-9 daemon payload.
        out.hardware_encoder = request.value("hardware", "");
    }
    out.encoder_backend = spec_or_root(request, "encoder_backend");
    out.ffmpeg_mode = spec_or_root(request, "ffmpeg_mode");
    out.gpu_hot_path_mode = spec_or_root(request, "gpu_hot_path_mode");
    out.encode_preset = spec_or_root(request, "encode_preset");
    out.rate_control_mode = spec_or_root(request, "rate_control_mode");
    out.crf = static_cast<int>(spec_or_root_int(request, "crf", -1));
    out.qp = static_cast<int>(spec_or_root_int(request, "qp", -1));
    out.bitrate = spec_or_root_int(request, "bitrate", 0);
    out.audio_source_path = request.value("audio_source_path", "");
    out.audio_target_sample_rate = static_cast<int>(
        spec_or_root_int(request, "audio_target_sample_rate", 0));
    out.gop_copy_only = request.value("gop_copy_only", false);
    const nlohmann::json* intervals = nullptr;
    const auto spec_it = request.find("output_spec");
    if (spec_it != request.end() && spec_it->is_object() &&
        spec_it->contains("changed_intervals")) {
        intervals = &(*spec_it)["changed_intervals"];
    } else if (request.contains("changed_intervals")) {
        intervals = &request["changed_intervals"];
    }
    if (intervals) {
        if (!intervals->is_array()) {
            throw std::runtime_error("changed_intervals must be a JSON array");
        }
        for (const auto& item : *intervals) {
            if (!item.is_object()) {
                throw std::runtime_error("changed_intervals entries must be objects");
            }
            const auto kind = item.value("kind", std::string{"content"});
            media::ChangedIntervalKind interval_kind;
            if (kind == "content") {
                interval_kind = media::ChangedIntervalKind::Content;
            } else if (kind == "trim") {
                interval_kind = media::ChangedIntervalKind::Trim;
            } else {
                throw std::runtime_error(
                    "changed_intervals kind must be content or trim");
            }
            out.changed_intervals.push_back(media::ChangedInterval{
                item.value("start_seconds", 0.0),
                item.value("end_seconds", 0.0), interval_kind});
        }
    }
    out.pipe_pixfmt = spec_or_root(request, "pipe_pixfmt");
    out.fps_num = static_cast<std::uint32_t>(
        spec_or_root_int(request, "fps_num", 0));
    out.fps_den = static_cast<std::uint32_t>(
        spec_or_root_int(request, "fps_den", 1));

    media::ExecutionRequirements exec_reqs;
    if (request.contains("execution_requirements")) {
        const auto& sr = request["execution_requirements"];
        if (!sr.is_object()) {
            throw std::runtime_error(
                "execution_requirements must be a JSON object");
        }
        const std::string requirements_backend = sr.value("backend", "");
        if (job_request.backend == "auto" &&
            !requirements_backend.empty() && requirements_backend != "auto") {
            // Mirrors the CLI subprocess --backend flag the RenderingGen
            // adapter emits from ExecutionRequirements.Backend.
            job_request.backend = requirements_backend;
        }
        exec_reqs.gpu_required = sr.value("gpu_required", false);
        exec_reqs.cpu_fallback_allowed =
            sr.value("cpu_fallback_allowed", true);
        exec_reqs.composition_required =
            sr.value("composition_required", true);
        exec_reqs.video_source_required =
            sr.value("video_source_required", true);
        exec_reqs.packet_copy_allowed =
            sr.value("packet_copy_allowed", true);
    } else {
        // Legacy payload without a semantic requirements block: derive the
        // GPU requirement from the encoder selection exactly like the
        // pre-Phase-9 daemon did.
        exec_reqs.gpu_required = out.hardware_encoder == "nvenc";
    }
    job_request.requirements = exec_reqs;
    job_request.backend = request.value("backend", job_request.backend);
    return job_request;
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
                 m_render_count.load(std::memory_order_relaxed),
                 m_total_render_ms.load(std::memory_order_relaxed));
}

ipc::Reply DaemonService::handle_ipc(const ipc::Request& req) {
    switch (req.cmd) {
        // Handlers own their domain lock; the dispatcher holds none. A
        // compile or a disk write therefore cannot serialize STATUS, and the
        // RENDER_JOB path below still runs entirely lock-free on the IPC
        // state (it carries its own scheduler reservation).
        case ipc::Command::PrefetchAsset:
            return ipc_prefetch_asset(req.payload);
        case ipc::Command::PreparePlan:
            return ipc_prepare_plan(req.payload);
        case ipc::Command::RenderOverlay:
            return ipc_render_overlay(req.payload);
        case ipc::Command::RenderJob: {
            // Phase 9: decode ONCE (canonical mapper), normalize ONCE, then
            // hand the immutable NormalizedRenderJob to the render group.
            // No payload re-serialization, no second parse, no second
            // normalize anywhere on this path.
            runtime::job::RenderJobRequest job_request;
            try {
                job_request = decode_render_job_payload(req.payload);
            } catch (const std::exception& e) {
                return ipc::Reply{ipc::Status::BadRequest,
                                  std::string{"RENDER_JOB parse failed: "} + e.what()};
            }
            const auto normalized = runtime::job::normalize_render_job(
                job_request, runtime::job::RuntimeCapabilities{m_backend});
            if (!normalized) {
                return ipc::Reply{ipc::Status::BadRequest,
                                  "RENDER_JOB normalization failed: " +
                                      normalized.error().message};
            }
            const auto& resolved = normalized.value();
            const auto& canon = resolved.execution;
            // serve_concurrent keeps IPC ingress responsive, but a video job
            // owns a complete Vulkan/CUDA exporter and its frame-transient
            // surface ring. Running several such exporters on one device
            // multiplies 1080p surfaces and can exhaust VRAM before the
            // scheduler observes the real memory pressure. Serialize the
            // execution domain here; STATUS and asset operations remain
            // concurrent because they do not take this mutex.
            //
            // This mutex IS the concurrency contract: the advertised
            // DeviceScheduler capacity is resolved by the same authority, and
            // the static_asserts below make a one-sided change a build failure
            // instead of a capacity the daemon cannot honour.
            static_assert(kRenderJobExecutionSerialized,
                          "m_render_job_mutex is held for the whole RENDER_JOB: "
                          "kRenderJobExecutionSerialized must stay true until the "
                          "mutex is removed together with it (VRAM-safety decision)");
            static_assert(kSerializedRenderJobSlots == 1U,
                          "one execution-domain mutex admits exactly one render job");
            std::unique_lock<std::mutex> render_job_lock(m_render_job_mutex);
            const bool native_nvenc = canon.native_nvenc;
            const std::string& hot_path = canon.gpu_hot_path_mode;
            const std::string& codec = canon.codec;
            const bool direct_yuv = canon.direct_yuv;

            std::uint32_t plan_width = 0;
            std::uint32_t plan_height = 0;
            extract_plan_dimensions(resolved.plan_path, plan_width, plan_height);
            const uint64_t estimated_vram = estimate_peak_vram_bytes(
                plan_width, plan_height, direct_yuv, native_nvenc);

            runtime::DeviceSelectionRequirements requirements;
            requirements.resources.compute_units = native_nvenc ? 0.5f : 0.0f;
            requirements.cuda = native_nvenc;
            requirements.vulkan_interop = native_nvenc && hot_path != "require_direct_yuv";
            requirements.nvenc = native_nvenc;
            requirements.nv12 = native_nvenc;
            requirements.resources.nvenc_sessions = native_nvenc ? 1U : 0U;
            requirements.resources.render_job_slots = 1U;
            requirements.resources.vram_bytes = estimated_vram;
            requirements.resources.pcie_bandwidth = native_nvenc ? 0.25f : 0.0f;
            requirements.h264 = native_nvenc &&
                (codec == "auto" || codec == "h264" || codec == "libx264" ||
                 codec == "h264_nvenc");
            requirements.hevc = native_nvenc &&
                (codec == "hevc" || codec == "h265" || codec == "libx265" ||
                 codec == "hevc_nvenc");
            requirements.av1 = native_nvenc &&
                (codec == "av1" || codec == "libsvtav1" || codec == "av1_nvenc");

            // Admission control with backpressure:
            // Concurrency is governed dynamically by DeviceScheduler capacity
            // (render_job_slots and VRAM budget credits). When resources are saturated,
            // the IPC thread waits on m_admission_cv until an active job finishes and
            // returns its reservation to the scheduler.
            runtime::DeviceReservation reservation;
            {
                std::unique_lock<std::mutex> admission_lock(m_admission_mutex);
                const auto admission_start = std::chrono::steady_clock::now();
                const bool admitted = m_admission_cv.wait_for(
                    admission_lock, std::chrono::seconds(300), [&]() {
                        if (!m_running) return true;
                        auto res = m_device_scheduler.reserve(requirements);
                        if (res) {
                            reservation = std::move(*res);
                            return true;
                        }
                        return false;
                    });
                if (!m_running) {
                    return ipc::Reply{ipc::Status::Shutdown, "daemon is shutting down"};
                }
                if (!admitted || !reservation.valid()) {
                    return ipc::Reply{ipc::Status::Error,
                        "RENDER_JOB rejected: device resources saturated or admission timed out"};
                }
                const auto admission_wait_ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - admission_start).count();
                if (admission_wait_ms > 5.0) {
                    spdlog::info("[daemon] RENDER_JOB admission wait: {:.1f}ms (queue backpressure, vram_req={}MB)",
                                 admission_wait_ms, estimated_vram / (1024 * 1024));
                }
            }

            const auto reserved_device = reservation.device();
            spdlog::debug("[daemon] RENDER_JOB placed on device {} (native_nvenc={}, vram_est={}MB)",
                          reserved_device, native_nvenc, estimated_vram / (1024 * 1024));
            auto video_execution = std::make_shared<media::VideoJobExecutionContext>();
            video_execution->device_scheduler = &m_device_scheduler;
            // Per-job wall anchor for the exclusive-wall timeline: startup is
            // measured from THIS request, never from daemon boot.
            video_execution->job_started_at = profiling::now();
            video_execution->device_id = reserved_device;
            if (const auto caps = m_device_scheduler.capability_snapshot(reserved_device)) {
                video_execution->cuda_device_ordinal = caps->cuda_device_ordinal;
                video_execution->physical_device_index = caps->physical_device_index;
            }
            video_execution->reservation = std::move(reservation);
            video_execution->video_runtimes = m_video_runtimes;

            // Warm renderer is runtime state of ONE execution authority, not a
            // second dispatcher: null on the Direct-YUV path is a normal job.
            std::shared_ptr<SoftwareRenderer> warm_renderer;
            if (!direct_yuv) {
                warm_renderer = warm_renderer_for_device(reserved_device);
            } else {
                spdlog::debug("[daemon] Direct-YUV job: renderer construction skipped");
            }

            ipc::Reply reply;
            try {
                auto& dispatcher = render_job_dispatcher();
                if (dispatcher) {
                    reply = dispatcher(resolved, std::move(warm_renderer),
                                      std::move(video_execution));
                } else {
                    reply = ipc::Reply{ipc::Status::NotFound,
                                      "RENDER_JOB unavailable: render group not compiled"};
                }
            } catch (const std::exception& e) {
                spdlog::error("[daemon] RENDER_JOB dispatcher exception: {}", e.what());
                reply = ipc::Reply{ipc::Status::Error,
                                  std::string{"RENDER_JOB failed: "} + e.what()};
            }

            // Release execution context and reservation to return credits immediately
            video_execution.reset();

            // Wake any pending job waiting in admission
            m_admission_cv.notify_all();

            return reply;
        }
        case ipc::Command::AssembleSegments:
            return ipc_assemble_segments(req.payload);
        case ipc::Command::Status:
            return ipc_status();
        case ipc::Command::Shutdown:
            m_running = false;
            m_admission_cv.notify_all();
            return ipc::Reply{ipc::Status::Shutdown, "bye"};
        default:
            return ipc::Reply{ipc::Status::BadRequest, "unknown command"};
    }
}

ipc::Reply DaemonService::ipc_prefetch_asset(const std::string& path) {
    if (path.empty()) {
        return ipc::Reply{ipc::Status::BadRequest, "PREFETCH_ASSET requires an asset path"};
    }
    // RenderEngine mutation domain: the asset registry is engine-owned state.
    std::lock_guard<std::mutex> engine_lock(m_engine_mutex);
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
        // Compile + plan under the engine domain only. The prepared-plan slot
        // is published afterwards under its own mutex, so a long compile no
        // longer blocks STATUS or an in-flight RENDER_OVERLAY.
        std::shared_ptr<PreparedRenderJob> prepared;
        {
            std::lock_guard<std::mutex> engine_lock(m_engine_mutex);
            prepared = std::make_shared<PreparedRenderJob>(
                m_engine->prepare(comp));
        }
        std::size_t slots = 0;
        {
            std::lock_guard<std::mutex> lock(m_prepared_mutex);
            slots = prepared->resource_plan().slots.size();
            m_prepared_job = std::move(prepared);
            m_prepared_comp_id = comp_id;
        }
        spdlog::info("🧠 PREPARE_PLAN '{}' — resource plan slots: {}",
                     comp_id, slots);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(m_prepared_mutex);
        m_prepared_job.reset();
        m_prepared_comp_id.clear();
        return ipc::Reply{ipc::Status::Error, std::string{"prepare failed: "} + e.what()};
    }
    return ipc::Reply{ipc::Status::Ok, "plan ready"};
}

ipc::Reply DaemonService::ipc_render_overlay(const std::string& args) {
    const auto tokens = daemon_detail::split_args(args);
    if (tokens.empty()) {
        return ipc::Reply{ipc::Status::BadRequest,
                          "RENDER_OVERLAY requires '<frame> [output]'"};
    }
    // Snapshot the plan handle; the shared_ptr keeps it alive for the whole
    // render even if a concurrent PREPARE_PLAN publishes a replacement.
    std::shared_ptr<PreparedRenderJob> prepared;
    {
        std::lock_guard<std::mutex> lock(m_prepared_mutex);
        prepared = m_prepared_job;
    }
    if (!prepared) {
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

    // Overlay domain: the prepared job owns one single-job JobSession, so two
    // overlapping overlays on the same plan must serialize. This is the only
    // lock held on this path — STATUS, PREFETCH, PREPARE_PLAN and RENDER_JOB
    // all proceed while the render + disk write below run.
    std::lock_guard<std::mutex> overlay_lock(m_overlay_mutex);

    std::shared_ptr<Framebuffer> fb;
    try {
        fb = prepared->render(frame);
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

    m_render_count.fetch_add(1, std::memory_order_relaxed);
    return ipc::Reply{ipc::Status::Ok, output};
}

ipc::Reply DaemonService::ipc_assemble_segments(const std::string& payload) {
    // assemble_segments lives in chronon3d_media_native (libavformat MuxSession
    // authority), which exists only when native FFmpeg is enabled. The old
    // CHRONON3D_ENABLE_VIDEO guard was wrong and produced an undefined symbol
    // at link time in video-enabled / native-ffmpeg-disabled presets.
#if defined(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    try {
        const auto request = nlohmann::json::parse(payload.begin(), payload.end());
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
                      std::string{"ASSEMBLE_SEGMENTS unavailable: built without native FFmpeg mux"}};
#endif
}

ipc::Reply DaemonService::ipc_status() {
    // STATUS takes no execution-domain lock: counters are atomic and the
    // prepared handle is read under its own short-lived mutex.
    std::string prepared_comp;
    {
        std::lock_guard<std::mutex> lock(m_prepared_mutex);
        prepared_comp = m_prepared_comp_id.empty() ? "(none)" : m_prepared_comp_id;
    }
    std::ostringstream ss;
    ss << "frames_rendered=" << m_render_count.load(std::memory_order_relaxed)
       << " total_ms=" << m_total_render_ms.load(std::memory_order_relaxed)
       << " prepared_comp=" << prepared_comp;
    return ipc::Reply{ipc::Status::Ok, ss.str()};
}

} // namespace chronon3d::cli
