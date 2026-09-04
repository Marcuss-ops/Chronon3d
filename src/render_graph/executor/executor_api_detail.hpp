[[nodiscard]] static runtime::FrameExecutionResult frame_result_from_session(
    const RenderSession& session) {
    const auto error = session.last_frame_error();
    if (runtime::gpu_worker_poisoned()) {
        std::string reason = "GPU worker is poisoned after a terminal device-loss failure";
        if (error && !error->message.empty()) {
            reason = error->message;
        }
        return runtime::FrameExecutionResult::failed(
            runtime::ExecutionFailure::DeviceLost, std::move(reason));
    }
    if (!error) {
        return runtime::FrameExecutionResult::failed(
            runtime::ExecutionFailure::InvalidResource,
            "graph execution returned no framebuffer without a diagnostic");
    }

    runtime::ExecutionFailure failure = runtime::ExecutionFailure::InvalidResource;
    switch (error->backend_code) {
        case RenderBackendErrorCode::UnsupportedCapability:
        case RenderBackendErrorCode::InvalidInput:
            failure = runtime::ExecutionFailure::InvalidResource;
            break;
        case RenderBackendErrorCode::ExecutionFailure:
            failure = runtime::ExecutionFailure::InvalidResource;
            break;
    }

    std::string reason;
    if (!error->node_name.empty()) {
        reason = error->node_name + ": ";
    }
    reason += error->message;
    return runtime::FrameExecutionResult::failed(failure, std::move(reason));
}

FrameExecutionOutput GraphExecutor::execute(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    ExecutionScope& scope,
    ExecutionScheduler& scheduler
) const {
    auto& session = scope.session();
    try {
        auto framebuffer = execute_internal(
            compiled, ctx, session, scope.arena(), scheduler);
        if (framebuffer) {
            return FrameExecutionOutput{
                .framebuffer = std::move(framebuffer),
                .result = runtime::FrameExecutionResult::succeeded(),
            };
        }
        return FrameExecutionOutput{
            .framebuffer = nullptr,
            .result = frame_result_from_session(session),
        };
    } catch (const runtime::GpuDeviceLostError& error) {
        return FrameExecutionOutput{
            .framebuffer = nullptr,
            .result = runtime::FrameExecutionResult::failed(
                runtime::ExecutionFailure::DeviceLost, error.what()),
        };
    }
}
