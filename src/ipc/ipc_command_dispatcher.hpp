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

    IpcResponse handle(const IpcShutdown&) {
        m_runtime->begin_shutdown();
        return IpcResponse{IpcShutdownResult{IpcStatus_Shutdown, "bye"}};
    }

    std::unique_ptr<daemon::DaemonRuntime> m_runtime;
};

} // namespace chronon3d::ipc
