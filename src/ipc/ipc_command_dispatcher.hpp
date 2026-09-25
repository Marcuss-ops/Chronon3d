// ---------------------------------------------------------------------------
// src/ipc/ipc_command_dispatcher.hpp — IpcRequest → Chronon Runtime bridge
// ---------------------------------------------------------------------------

#pragma once

#include "ipc_codec.hpp"
#include "contract_validator_registry.hpp"
#include "../daemon/daemon_runtime.hpp"

#include <chronon3d/api/render_engine.hpp>
#include <memory>
#include <string>

namespace chronon3d::ipc {

class IpcCommandDispatcher {
public:
    explicit IpcCommandDispatcher(
        std::unique_ptr<RenderEngine> engine,
        const ContractValidatorRegistry& validators = builtin_contract_validators())
        : m_runtime(std::make_unique<daemon::DaemonRuntime>(
              std::move(engine), validators)) {}

    void warmup() { m_runtime->warmup(); }

    [[nodiscard]] IpcResponse dispatch(const IpcRequest& request) {
        return std::visit([this](const auto& req) -> IpcResponse {
            return handle(req);
        }, request);
    }

    [[nodiscard]] RenderEngine& engine() noexcept { return m_runtime->engine(); }

    [[nodiscard]] std::string status_json() const {
        return m_runtime->status_json();
    }

private:
    IpcResponse handle(const IpcCreateComposition& req) {
        try {
            m_runtime->prepare_composition(req.composition_id, req.descriptor_json);
            return IpcResponse{IpcCreateCompositionResult{IpcStatus_Ok, "ok"}};
        } catch (const std::exception& e) {
            return IpcResponse{IpcCreateCompositionResult{IpcStatus_Error, e.what()}};
        }
    }

    IpcResponse handle(const IpcRenderFrame& req) {
        if (!m_runtime->contains(req.composition_id)) {
            return IpcResponse{IpcRenderFrameResult{
                IpcStatus_NotPrepared, "not prepared", "", 0.0f}};
        }

        Frame frame(req.frame_index);
        auto outcome = m_runtime->render_frame_timed(req.composition_id, frame);
        auto& fb = outcome.framebuffer;
        const auto render_ms = static_cast<float>(outcome.render_ms);

        if (!fb) {
            return IpcResponse{IpcRenderFrameResult{
                IpcStatus_Error, "render returned null framebuffer", "", render_ms}};
        }

        return IpcResponse{IpcRenderFrameResult{
            IpcStatus_Ok, "ok", req.output_path, render_ms}};
    }

    IpcResponse handle(const IpcStatusRequest&) {
        return IpcResponse{IpcStatusResult{IpcStatus_Ok, status_json()}};
    }

    IpcResponse handle(const IpcPrepareJob& req) {
        try {
            m_runtime->prepare_job(req.job_id, req.descriptor_json);
            return job_reply(req.job_id, IpcStatus_Ok, "prepared", JobState_Prepared);
        } catch (const std::exception& e) {
            m_runtime->record_job_failure(req.job_id, e.what());
            const auto status = m_runtime->job_status(req.job_id);
            return job_reply(req.job_id, IpcStatus_Error, e.what(),
                status ? to_wire_state(status->state) : JobState_Failed,
                status ? status->last_frame : 0,
                status ? static_cast<float>(status->render_ms) : 0.0f);
        } catch (...) {
            m_runtime->record_job_failure(req.job_id, "non-standard prepare exception");
            const auto status = m_runtime->job_status(req.job_id);
            return job_reply(req.job_id, IpcStatus_Error,
                "non-standard prepare exception",
                status ? to_wire_state(status->state) : JobState_Failed,
                status ? status->last_frame : 0,
                status ? static_cast<float>(status->render_ms) : 0.0f);
        }
    }

    IpcResponse handle(const IpcRenderJob& req) {
        const auto before = m_runtime->job_status(req.job_id);
        if (!before) return job_reply(req.job_id, IpcStatus_NotFound, "unknown job");
        if (before->state == SessionComposition::JobState::Released) {
            return job_reply(req.job_id, IpcStatus_NotPrepared, "job released",
                             JobState_Released);
        }
        if (!m_runtime->accepting()) {
            return job_reply(req.job_id, IpcStatus_Shutdown,
                             "daemon is not accepting jobs",
                             to_wire_state(before->state));
        }
        daemon::DaemonRuntime::RenderOutcome outcome;
        try {
            outcome = m_runtime->render_job(
                req.job_id, Frame(req.frame_index));
        } catch (const std::exception& e) {
            const auto failed = m_runtime->job_status(req.job_id);
            return job_reply(req.job_id, IpcStatus_Error, e.what(),
                failed ? to_wire_state(failed->state) : JobState_Failed);
        } catch (...) {
            return job_reply(req.job_id, IpcStatus_Error,
                             "non-standard render exception", JobState_Failed);
        }
        const auto status = m_runtime->job_status(req.job_id);
        const auto wire_state = status ? to_wire_state(status->state) : JobState_Unknown;
        if (!outcome.framebuffer) {
            return job_reply(req.job_id, IpcStatus_Error,
                status ? status->message : "render returned null framebuffer",
                wire_state, status ? status->last_frame : 0,
                static_cast<float>(outcome.render_ms));
        }
        return job_reply(req.job_id, IpcStatus_Ok, "completed", wire_state,
                         status ? status->last_frame : req.frame_index,
                         static_cast<float>(outcome.render_ms));
    }

    IpcResponse handle(const IpcReleaseJob& req) {
        if (!m_runtime->release_job(req.job_id)) {
            return job_reply(req.job_id, IpcStatus_NotFound, "unknown job");
        }
        return job_reply(req.job_id, IpcStatus_Ok, "released", JobState_Released);
    }

    IpcResponse handle(const IpcGetJobStatus& req) {
        const auto status = m_runtime->job_status(req.job_id);
        if (!status) return job_reply(req.job_id, IpcStatus_NotFound, "unknown job");
        return job_reply(req.job_id, IpcStatus_Ok, status->message,
                         to_wire_state(status->state), status->last_frame,
                         static_cast<float>(status->render_ms));
    }

    IpcResponse handle(const IpcHealthRequest&) {
        const auto body = m_runtime->health_json();
        const bool healthy = m_runtime->accepting();
        return IpcResponse{IpcHealthResult{
            healthy ? IpcStatus_Ok : IpcStatus_Error, body}};
    }

    IpcResponse handle(const IpcShutdown&) {
        m_runtime->begin_shutdown();
        return IpcResponse{IpcShutdownResult{IpcStatus_Shutdown, "bye"}};
    }

    static JobState to_wire_state(SessionComposition::JobState state) noexcept {
        switch (state) {
        case SessionComposition::JobState::Prepared: return JobState_Prepared;
        case SessionComposition::JobState::Running: return JobState_Running;
        case SessionComposition::JobState::Completed: return JobState_Completed;
        case SessionComposition::JobState::Failed: return JobState_Failed;
        case SessionComposition::JobState::Released: return JobState_Released;
        case SessionComposition::JobState::Unknown: break;
        }
        return JobState_Unknown;
    }

    IpcResponse job_reply(std::string job_id, IpcStatus status,
                          std::string message,
                          JobState state = JobState_Unknown,
                          std::uint32_t last_frame = 0,
                          float render_ms = 0.0f) const {
        return IpcResponse{IpcJobResult{status, std::move(job_id), state,
            std::move(message), last_frame, render_ms}};
    }

    std::unique_ptr<daemon::DaemonRuntime> m_runtime;
};

} // namespace chronon3d::ipc
