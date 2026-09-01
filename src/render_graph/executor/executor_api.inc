std::shared_ptr<Framebuffer> GraphExecutor::execute(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    RenderSession& session,
    ExecutionScheduler& scheduler
) const {
    return execute_internal(compiled, ctx, session, session.arena(), scheduler);
}

std::shared_ptr<Framebuffer> GraphExecutor::execute_with_scope(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    ExecutionScope& scope,
    ExecutionScheduler& scheduler
) const {
    return execute_internal(
        compiled, ctx, scope.session(), scope.arena(), scheduler);
}
