#include "daemon_service.hpp"
#include "daemon_render_concurrency.hpp"
#include "daemon_render_job_payload.hpp"
#include "daemon_service_internal.hpp"
#include "daemon_service_ipc_protocol.hpp"
#include "daemon_service_job_admission.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/runtime/job/render_job_request.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::cli {

ipc::Reply DaemonService::handle_render_job_payload(const std::string& payload) {
            // Phase 9: decode ONCE (canonical mapper), normalize ONCE, then
            // hand the immutable NormalizedRenderJob to the render group.
            // No payload re-serialization, no second parse, no second
            // normalize anywhere on this path.
            runtime::job::RenderJobRequest job_request;
            std::string job_id;
            try {
                auto decoded = decode_render_job_payload(payload);
                job_id = std::move(decoded.job_id);
                job_request = std::move(decoded.request);
            } catch (const std::exception& e) {
                return ipc::Reply{ipc::Status::BadRequest,
                                  std::string{"RENDER_JOB parse failed: "} + e.what()};
            }
            return handle_render_job_request(std::move(job_request), std::move(job_id));
}

ipc::Reply DaemonService::handle_render_job_request(
    runtime::job::RenderJobRequest job_request, std::string job_id,
    bool batch_member) {
            std::shared_lock<std::shared_mutex> single_job_guard;
            if (!batch_member) {
                single_job_guard = std::shared_lock<std::shared_mutex>(
                    m_render_batch_domain_mutex);
            }
            if (!job_id.empty() && !begin_job_status(job_id)) {
                return ipc::Reply{ipc::Status::BadRequest,
                    "RENDER_JOB duplicate active or retained job_id '" + job_id + "'"};
            }
            const auto normalized = runtime::job::normalize_render_job(
                job_request, runtime::job::RuntimeCapabilities{m_backend});
            if (!normalized) {
                update_job_status(job_id, JobState::Failed,
                                  normalized.error().message);
                return ipc::Reply{ipc::Status::BadRequest,
                                  "RENDER_JOB normalization failed: " +
                                      normalized.error().message};
            }
            update_job_status(job_id, JobState::Prepared, "normalized");
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
                    update_job_status(job_id, JobState::Failed,
                                      "plan file unreadable; canvas dimensions unknown");
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

            // One RAII context owns the policy-selected execution lock and
            // scheduler reservation. It releases both on normal return,
            // shutdown, parse/admission failure and dispatcher exceptions.
            RenderJobAdmissionContext admission(
                resolved.parallel_disjoint, estimated_vram,
                m_render_job_mutex, m_admission_mutex, m_admission_cv,
                m_running, m_device_scheduler, requirements);
            const auto& concurrency = admission.decision();
            spdlog::debug(
                "[daemon] render concurrency verdict: class={} admission={} "
                "slots={} peak={} MiB footprints_that_fit={} reason={}",
                resolved.parallel_disjoint ? "chunked_disjoint" : "monolithic",
                concurrency.admission == RenderAdmission::Serial
                    ? "serial" : "parallel_disjoint",
                concurrency.admitted_slots,
                estimated_vram / (1024ULL * 1024ULL),
                concurrency.peak_footprints_that_fit, concurrency.reason);
            if (!admission.admitted()) {
                update_job_status(job_id, JobState::Failed,
                                  admission.failure_reason());
                if (std::string_view{admission.failure_reason()} ==
                    "daemon is shutting down") {
                    return ipc::Reply{ipc::Status::Shutdown,
                                      admission.failure_reason()};
                }
                return ipc::Reply{ipc::Status::Error,
                                  std::string{"RENDER_JOB rejected: "} +
                                      admission.failure_reason()};
            }

            auto reservation = admission.take_reservation();
            update_job_status(job_id, JobState::Running, "rendering");
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
            {
                std::lock_guard lock(m_video_render_states_mutex);
                if (const auto it = m_video_render_states.find(reserved_device);
                    it != m_video_render_states.end()) {
                    video_execution->persistent_render_state = it->second;
                }
            }

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
                                      video_execution);
                } else {
                    reply = ipc::Reply{ipc::Status::NotFound,
                                      "RENDER_JOB unavailable: render group not compiled"};
                }
            } catch (const std::exception& e) {
                spdlog::error("[daemon] RENDER_JOB dispatcher exception: {}", e.what());
                reply = ipc::Reply{ipc::Status::Error,
                                  std::string{"RENDER_JOB failed: "} + e.what()};
            }

            if (video_execution->persistent_render_state) {
                std::lock_guard lock(m_video_render_states_mutex);
                m_video_render_states.insert_or_assign(
                    reserved_device, video_execution->persistent_render_state);
            }

            // Release execution context and reservation to return credits immediately
            video_execution.reset();

            // Wake any pending job waiting in admission
            m_admission_cv.notify_all();

            update_job_status(job_id,
                reply.status == ipc::Status::Ok ? JobState::Completed : JobState::Failed,
                reply.message);

            return reply;
}

ipc::Reply DaemonService::ipc_render_batch(const std::string& payload) {
    std::vector<std::pair<runtime::job::RenderJobRequest, std::string>> jobs;
    try {
        auto decoded_jobs = decode_render_batch_payload(payload);
        jobs.reserve(decoded_jobs.size());
        for (auto& decoded : decoded_jobs) {
            jobs.emplace_back(std::move(decoded.request), std::move(decoded.job_id));
        }
    } catch (const std::exception& e) {
        return ipc::Reply{ipc::Status::BadRequest,
            std::string{"RENDER_BATCH parse failed: "} + e.what()};
    }

    // Once admitted, the sequence is one render-domain transaction: a
    // standalone RENDER_JOB waits until all batch outputs finish, so device
    // and renderer warm state stays useful across adjacent jobs.
    std::unique_lock<std::shared_mutex> batch_guard(m_render_batch_domain_mutex);
    nlohmann::json results = nlohmann::json::array();
    bool all_ok = true;
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        ipc::Reply reply;
        try {
            reply = handle_render_job_request(
                std::move(jobs[i].first), std::move(jobs[i].second), true);
        } catch (const std::exception& e) {
            reply = ipc::Reply{ipc::Status::Error,
                std::string{"RENDER_BATCH job failed: "} + e.what()};
        } catch (...) {
            reply = ipc::Reply{ipc::Status::Error,
                "RENDER_BATCH job failed with an unknown exception"};
        }
        all_ok = all_ok && reply.status == ipc::Status::Ok;
        results.push_back({
            {"index", i},
            {"status", reply.status == ipc::Status::Ok ? "ok" : "error"},
            {"message", std::move(reply.message)}});
    }
    return ipc::Reply{all_ok ? ipc::Status::Ok : ipc::Status::Error,
        nlohmann::json{{"results", std::move(results)}}.dump()};
}

} // namespace chronon3d::cli
