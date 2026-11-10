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
    // The composition session (create/prepare/render/release frame) was the
    // legacy CompiledComposition executor's IPC surface. It is retired: every
    // request that used it is answered with an explicit error instead of
    // silently falling back to a second execution path.
    IpcResponse handle(const IpcCreateComposition&) {
        return IpcResponse{IpcCreateCompositionResult{IpcStatus_Error,
            "CreateComposition was retired with the legacy Composition executor"}};
    }

    IpcResponse handle(const IpcRenderFrame&) {
        return IpcResponse{IpcRenderFrameResult{IpcStatus_NotPrepared,
            "RenderFrame was retired with the legacy Composition executor",
            "", 0.0f}};
    }

    IpcResponse handle(const IpcStatusRequest&) {
        return IpcResponse{IpcStatusResult{IpcStatus_Ok, status_json()}};
    }

    IpcResponse handle(const IpcPrepareJob& req) {
        return job_reply(req.job_id, IpcStatus_Error,
            "PrepareJob was retired with the legacy Composition executor",
            JobState_Failed);
    }

    IpcResponse handle(const IpcRenderJob& req) {
        return job_reply(req.job_id, IpcStatus_Error,
            "RenderJob was retired with the legacy Composition executor",
            JobState_Failed);
    }

    IpcResponse handle(const IpcReleaseJob& req) {
        return job_reply(req.job_id, IpcStatus_Error,
            "ReleaseJob was retired with the legacy Composition executor",
            JobState_Failed);
    }

    IpcResponse handle(const IpcGetJobStatus& req) {
        return job_reply(req.job_id, IpcStatus_NotFound, "unknown job");
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
