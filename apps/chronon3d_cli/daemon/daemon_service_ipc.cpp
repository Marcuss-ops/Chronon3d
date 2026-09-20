#include "daemon_service.hpp"
#include "daemon_render_concurrency.hpp"
#include "daemon_render_job_payload.hpp"
#include "daemon_service_internal.hpp"
#include "daemon_service_ipc_protocol.hpp"
#include "daemon_service_job_admission.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>
#include <chronon3d/runtime/job/render_job_request.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

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

// ── The RENDER_JOB payload vocabulary (closed) ──────────────────────────────
//
// Union of what the RenderingGen client actually sends
// (renderinggen/internal/chronon/client.go: renderJobPayload, OutputSpec,
// ExecutionRequirements) and the legacy CLI-parity keys this decoder still
// accepts (see daemon_render_job_payload.hpp). Kept here, next to the decoder,
// so the vocabulary and the code that reads it cannot drift.
//
// A key outside this set is a CONTRACT VIOLATION, not a harmless extra. The
// plan-file ingress is validated against a schema with
// `additionalProperties: false`; the IPC payload was not, so the exact same
// typo was refused in one ingress path and silently ignored in the other — the
// job then ran with the built-in default for a field the caller believed it had
// set ("hardware_encder", "gop_copyonly", a mistyped output_spec). One
// vocabulary, one answer, both boundaries.
constexpr std::string_view kRenderJobRootKeys[] = {
    "plan_path", "prepared_package_path", "output", "assets_root", "report",
    "trace_output", "trace_level", "receipt_verify",
    "range_enabled", "first_frame", "last_frame", "parallel_disjoint",
    "codec", "hardware_encoder", "hardware", "encoder_backend",
    "ffmpeg_mode", "gpu_hot_path_mode", "encode_preset", "rate_control_mode",
    "crf", "qp", "bitrate", "audio_source_path",
    "audio_target_sample_rate", "gop_copy_only", "changed_intervals",
    "pipe_pixfmt", "fps_num", "fps_den", "backend",
    "execution_requirements", "output_spec",
};

constexpr std::string_view kRenderJobOutputSpecKeys[] = {
    "codec", "width", "height", "fps_num", "fps_den", "pipe_pixfmt",
    "hardware_encoder", "encoder_backend", "gpu_hot_path_mode",
    "encode_preset", "rate_control_mode", "crf", "qp", "bitrate",
    "audio_target_sample_rate", "changed_intervals", "ffmpeg_mode",
};

constexpr std::string_view kRenderJobRequirementKeys[] = {
    "backend", "gpu_required", "cpu_fallback_allowed",
    "composition_required", "video_source_required", "packet_copy_allowed",
};

constexpr std::string_view kChangedIntervalKeys[] = {
    "start_seconds", "end_seconds", "kind",
};

template <std::size_t N>
[[nodiscard]] bool payload_key_known(std::string_view key,
                                     const std::string_view (&keys)[N]) noexcept {
    for (const auto candidate : keys) {
        if (key == candidate) return true;
    }
    return false;
}

void refuse_unknown_key(std::vector<std::string>& unknown, std::string_view scope,
                        std::string_view key) {
    unknown.push_back(std::string{scope} + "." + std::string{key});
}

/// Refuse any key the payload vocabulary does not contain. Also refuses a
/// present-but-mistyped `output_spec`/`execution_requirements`: both are read
/// with an `is_object()` guard, so a string there used to mean "no output
/// spec at all" rather than an error.
void reject_unknown_render_job_keys(const nlohmann::json& request) {
    if (!request.is_object()) {
        throw std::runtime_error("RENDER_JOB payload must be a JSON object");
    }
    std::vector<std::string> unknown;
    for (const auto& item : request.items()) {
        if (!payload_key_known(item.key(), kRenderJobRootKeys)) {
            refuse_unknown_key(unknown, "payload", item.key());
        }
    }
    if (const auto spec = request.find("output_spec"); spec != request.end()) {
        if (!spec->is_object()) {
            throw std::runtime_error(
                "RENDER_JOB output_spec must be a JSON object");
        }
        for (const auto& item : spec->items()) {
            if (!payload_key_known(item.key(), kRenderJobOutputSpecKeys)) {
                refuse_unknown_key(unknown, "output_spec", item.key());
            }
        }
    }
    if (const auto reqs = request.find("execution_requirements");
        reqs != request.end()) {
        if (!reqs->is_object()) {
            throw std::runtime_error(
                "RENDER_JOB execution_requirements must be a JSON object");
        }
        for (const auto& item : reqs->items()) {
            if (!payload_key_known(item.key(), kRenderJobRequirementKeys)) {
                refuse_unknown_key(unknown, "execution_requirements", item.key());
            }
        }
    }
    // `changed_intervals` is read from BOTH scopes (root and `output_spec`), so
    // the closed vocabulary must cover both, entry keys included: a mistyped
    // `start_secnds` inside a nested array is the exact silent-default typo
    // this gate exists to refuse. Validating only the root array left the
    // nested one unchecked.
    const auto reject_interval_entry_keys =
        [&unknown](const nlohmann::json& container, std::string_view scope) {
            const auto intervals = container.find("changed_intervals");
            if (intervals == container.end() || !intervals->is_array()) return;
            for (const auto& entry : *intervals) {
                if (!entry.is_object()) continue;  // type error reported below
                for (const auto& item : entry.items()) {
                    if (!payload_key_known(item.key(), kChangedIntervalKeys)) {
                        refuse_unknown_key(unknown, scope, item.key());
                    }
                }
            }
        };
    reject_interval_entry_keys(request, "changed_intervals[]");
    if (const auto spec = request.find("output_spec");
        spec != request.end() && spec->is_object()) {
        reject_interval_entry_keys(*spec, "output_spec.changed_intervals[]");
    }
    if (unknown.empty()) return;

    std::string message = "RENDER_JOB payload contains unknown key(s): ";
    for (std::size_t index = 0; index < unknown.size(); ++index) {
        if (index != 0) message += ", ";
        message += unknown[index];
    }
    message +=
        ". The payload vocabulary is closed (daemon_render_job_payload.hpp): a "
        "key that is not in it would have been ignored silently, so the job "
        "would have run with a default the caller did not ask for. Remove the "
        "key or spell it as the contract does.";
    throw std::runtime_error(message);
}

} // namespace

RenderJobDispatcher& render_job_dispatcher() {
    static RenderJobDispatcher dispatcher;
    return dispatcher;
}

runtime::job::RenderJobRequest decode_render_job_payload(
    const std::string& payload) {
    const auto request = nlohmann::json::parse(payload);
    // Untrusted-input boundary: the vocabulary is closed, exactly like the
    // plan-file ingress (additionalProperties: false).
    reject_unknown_render_job_keys(request);

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
    // This is an explicit producer contract. A frame range by itself is not
    // enough to authorize overlap: it may be a seek/export request whose
    // complementary range is not owned by another job.
    job_request.parallel_disjoint = request.value("parallel_disjoint", false);

    auto& out = job_request.output;
    out.codec = spec_or_root(request, "codec");
    // AUDIT P1-2 — this is the untrusted-input boundary for the field: the
    // payload is spelled by the client, while every consumer downstream (the
    // requirement builder, the encoder resolution, the timing sidecar) expects
    // the canonical token. Normalize once, here, through the canonical
    // vocabulary instead of comparing literals at ~10 call sites.
    out.hardware_encoder = chronon3d::media::normalize_hardware_encoder_field(
        spec_or_root(request, "hardware_encoder"));
    if (out.hardware_encoder.empty()) {
        // Legacy alias accepted by the pre-Phase-9 daemon payload.
        out.hardware_encoder = chronon3d::media::normalize_hardware_encoder_field(
            request.value("hardware", ""));
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
        exec_reqs.gpu_required =
            chronon3d::media::hardware_encoder_requests_nvenc(out.hardware_encoder);
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
            // scheduler observes the real memory pressure. The execution
            // domain is therefore gated HERE; STATUS and asset operations
            // remain concurrent because they do not take this lock.
            //
            // WHICH side of the lock is taken is not decided here: it is the
            // verdict of the concurrency policy, made on the measured per-job
            // peak and the device's own memory. Every producer today submits a
            // monolithic same-plan job, whose verdict is Serial by
            // construction, so this is an exclusive lock for every render the
            // daemon currently receives. A job the policy admits as
            // ParallelDisjoint (a disjoint chunk of a plan with room for a
            // second peak footprint) takes the shared side instead.
            const bool native_nvenc = canon.native_nvenc;
            const std::string& hot_path = canon.gpu_hot_path_mode;
            const std::string& codec = canon.codec;
            const bool direct_yuv = canon.direct_yuv;

            std::uint32_t plan_width = 0;
            std::uint32_t plan_height = 0;
            switch (extract_plan_dimensions(resolved.plan_path, plan_width,
                                           plan_height)) {
                case PlanDimensions::Unreadable:
                    // AUDIT §6 — fail closed instead of charging 1920x1080.
                    return ipc::Reply{
                        ipc::Status::Error,
                        "RENDER_JOB rejected: plan file unreadable, canvas "
                        "dimensions unknown, refusing to charge an invented "
                        "VRAM footprint"};
                case PlanDimensions::NoCanvasField:
                    spdlog::warn(
                        "[daemon] plan '{}' carries no canvas: admission uses the "
                        "pre-compile floor until the compiled footprint exists",
                        resolved.plan_path.string());
                    break;
                case PlanDimensions::NotRequested:
                case PlanDimensions::Resolved:
                    break;
            }
            // P0-2/4: single policy authority — no second ad-hoc builder.
            auto requirements = resolve_device_requirements(
                plan_width, plan_height, direct_yuv, native_nvenc, codec, hot_path);
            const auto estimated_vram = requirements.resources.vram_bytes;

            // The concurrency verdict for THIS job, charged with the peak
            // admission just estimated for it. Only the explicitly marked
            // disjoint chunk class can receive a shared execution lock; a
            // monolithic render remains serial even when the card has spare
            // memory.
            const RenderPolicyDecision concurrency = decide_render_job_admission(
                resolved.parallel_disjoint, estimated_vram);
            std::unique_lock<std::shared_mutex> render_job_lock(
                m_render_job_mutex, std::defer_lock);
            std::shared_lock<std::shared_mutex> render_job_shared_lock(
                m_render_job_mutex, std::defer_lock);
            if (concurrency.admission == RenderAdmission::ParallelDisjoint) {
                render_job_shared_lock.lock();
            } else {
                render_job_lock.lock();
            }
            spdlog::debug(
                "[daemon] render concurrency verdict: class={} admission={} "
                "slots={} peak={} MiB footprints_that_fit={} reason={}",
                resolved.parallel_disjoint ? "chunked_disjoint" : "monolithic",
                concurrency.admission == RenderAdmission::Serial
                    ? "serial" : "parallel_disjoint",
                concurrency.admitted_slots,
                estimated_vram / (1024ULL * 1024ULL),
                concurrency.peak_footprints_that_fit, concurrency.reason);

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



} // namespace chronon3d::cli
