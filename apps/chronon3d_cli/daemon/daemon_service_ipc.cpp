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

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
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

enum class PlanDimensions : std::uint8_t {
    NotRequested,   // no plan path, or the caller already knows the canvas
    Resolved,       // canvas (or width+height) read from the plan
    NoCanvasField,  // plan parsed, but it carries no canvas: unknown, not invented
    Unreadable,     // plan missing or corrupt: fail closed, never guess
};

/// Read the plan's canvas dimensions for the admission charge.
///
/// AUDIT §6 — this used to swallow every failure with `catch (...) {}` and then
/// substitute hardcoded 1920x1080. Those dimensions are not cosmetic: they feed
/// `resolve_device_requirements(...)` and therefore the VRAM admission charge,
/// so an unreadable plan silently under- or over-charged the device with no log
/// and no way for the caller to notice. A plan that cannot be read is now
/// reported (`Unreadable`) and the caller refuses the job; a plan that simply
/// has no canvas is reported as unknown (`NoCanvasField`) so the pre-compile
/// floor stays the honest answer instead of a manufactured one.
PlanDimensions extract_plan_dimensions(const std::filesystem::path& plan_path,
                                      std::uint32_t& width, std::uint32_t& height) {
    if (width > 0 && height > 0) return PlanDimensions::NotRequested;
    if (plan_path.empty()) return PlanDimensions::NotRequested;
    std::ifstream file(plan_path);
    if (!file.is_open()) {
        spdlog::error("[daemon] plan dimensions unavailable: cannot open '{}'",
                      plan_path.string());
        return PlanDimensions::Unreadable;
    }
    try {
        nlohmann::json plan_json;
        file >> plan_json;
        if (plan_json.contains("canvas") && plan_json["canvas"].is_object()) {
            width = plan_json["canvas"].value("width", 0U);
            height = plan_json["canvas"].value("height", 0U);
        } else if (plan_json.contains("width") && plan_json.contains("height")) {
            width = plan_json.value("width", 0U);
            height = plan_json.value("height", 0U);
        }
    } catch (const std::exception& exception) {
        spdlog::error("[daemon] plan dimensions unavailable: '{}' is not a readable "
                      "plan ({})", plan_path.string(), exception.what());
        width = 0;
        height = 0;
        return PlanDimensions::Unreadable;
    }
    return (width > 0 && height > 0) ? PlanDimensions::Resolved
                                     : PlanDimensions::NoCanvasField;
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
    "range_enabled", "first_frame", "last_frame",
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
