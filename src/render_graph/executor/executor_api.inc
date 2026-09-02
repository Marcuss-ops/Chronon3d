[[nodiscard]] static runtime::FrameExecutionResult frame_result_from_session(
    const RenderSession& session) {
    const auto error = session.last_frame_error();
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

FrameExecutionOutput GraphExecutor::execute_with_result(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    RenderSession& session,
    ExecutionScheduler& scheduler
) const {
    auto framebuffer = execute_internal(compiled, ctx, session, session.arena(), scheduler);
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
}

FrameExecutionOutput GraphExecutor::execute_with_scope_result(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    ExecutionScope& scope,
    ExecutionScheduler& scheduler
) const {
    auto& session = scope.session();
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
}

std::shared_ptr<Framebuffer> GraphExecutor::execute(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    RenderSession& session,
    ExecutionScheduler& scheduler
) const {
    return execute_with_result(compiled, ctx, session, scheduler).framebuffer;
}

std::shared_ptr<Framebuffer> GraphExecutor::execute_with_scope(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    ExecutionScope& scope,
    ExecutionScheduler& scheduler
) const {
    return execute_with_scope_result(compiled, ctx, scope, scheduler).framebuffer;
}
