// ---------------------------------------------------------------------------
// src/ipc/ipc_command_dispatcher.hpp — IpcRequest → Chronon Runtime bridge
// ---------------------------------------------------------------------------

#pragma once

#include "ipc_codec.hpp"
#include "composition_session.hpp"
#include "contract_validator_registry.hpp"

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <atomic>
#include <memory>
#include <sstream>
#include <string>

namespace chronon3d::ipc {

class IpcCommandDispatcher {
public:
    explicit IpcCommandDispatcher(
        std::unique_ptr<RenderEngine> engine,
        const ContractValidatorRegistry& validators = builtin_contract_validators())
        : m_engine(std::move(engine))
        , m_validators(&validators)
        , m_session(std::make_unique<CompositionSession>(*m_engine, *m_validators))
    {}

    [[nodiscard]] IpcResponse dispatch(const IpcRequest& request) {
        return std::visit([this](const auto& req) -> IpcResponse {
            return handle(req);
        }, request);
    }

    [[nodiscard]] RenderEngine& engine() noexcept { return *m_engine; }

    [[nodiscard]] std::string status_json() const {
        std::ostringstream ss;
        ss << R"({"frames_rendered":)" << m_frame_count.load()
           << R"(,"total_render_ms":)" << m_total_render_ms << "}";
        return ss.str();
    }

private:
    IpcResponse handle(const IpcCreateComposition& req) {
        try {
            m_session->create_composition(req.composition_id, req.descriptor_json);
            return IpcResponse{IpcCreateCompositionResult{IpcStatus_Ok, "ok"}};
        } catch (const std::exception& e) {
            return IpcResponse{IpcCreateCompositionResult{IpcStatus_Error, e.what()}};
        }
    }

    IpcResponse handle(const IpcRenderFrame& req) {
        if (!m_session->contains(req.composition_id)) {
            return IpcResponse{IpcRenderFrameResult{
                IpcStatus_NotPrepared, "not prepared", "", 0.0f}};
        }

        Frame frame(req.frame_index);
        const auto t0 = profiling::now();
        auto fb = m_session->render_frame(req.composition_id, frame);
        const auto render_ms = static_cast<float>(profiling::elapsed_ms(t0));

        if (!fb) {
            return IpcResponse{IpcRenderFrameResult{
                IpcStatus_Error, "render returned null framebuffer", "", render_ms}};
        }

        m_frame_count.fetch_add(1);
        m_total_render_ms += render_ms;
        return IpcResponse{IpcRenderFrameResult{
            IpcStatus_Ok, "ok", req.output_path, render_ms}};
    }

    IpcResponse handle(const IpcStatusRequest&) {
        return IpcResponse{IpcStatusResult{IpcStatus_Ok, status_json()}};
    }

    IpcResponse handle(const IpcShutdown&) {
        return IpcResponse{IpcShutdownResult{IpcStatus_Shutdown, "bye"}};
    }

    std::unique_ptr<RenderEngine> m_engine;
    const ContractValidatorRegistry* m_validators;
    std::unique_ptr<CompositionSession> m_session;
    std::atomic<std::uint64_t> m_frame_count{0};
    double m_total_render_ms{0.0};
};

} // namespace chronon3d::ipc
