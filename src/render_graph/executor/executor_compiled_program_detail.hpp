static bool execute_compiled_program(
    const CompiledFrameProgram& program,
    RenderGraphContext& ctx,
    ExecutionState& state,
    RenderCounters* counters)
{
    auto* backend = ctx.services.backend;
    if (!backend) {
        spdlog::error("[compiled-executor] fully_recorded program has no render backend");
        return false;
    }

    for (const auto& level : program.levels) {
        for (GraphNodeId node_id : level) {
            const CompiledOperation* op = nullptr;
            for (const auto& candidate : program.operations) {
                if (candidate.node == node_id) {
                    op = &candidate;
                    break;
                }
            }
            if (!op || !op->has_compiled_execute()) {
                spdlog::error(
                    "[compiled-executor] node {} has no compiled_execute in "
                    "a fully_recorded program",
                    static_cast<int>(node_id));
                return false;
            }

            if (counters) {
                counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
            }

            if (!op->compiled_execute(backend, *op)) {
                spdlog::error(
                    "[compiled-executor] compiled_execute failed for node {}",
                    static_cast<int>(node_id));
                return false;
            }
        }
    }
    return true;
}
