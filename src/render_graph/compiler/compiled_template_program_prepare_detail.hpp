// ── prepare() retains static-island candidates only ─────────────────────────
// No pixel surface producer or consumer is connected to this API yet. Never
// suppress node execution here: the executor's skip result is transparent and
// would erase a composite backdrop. The canonical per-node cache remains safe.
PreparedFrameProgram prepare(const CompiledTemplateProgram& program) {
    PreparedFrameProgram prepared;
    if (!program.compiled || program.static_regions.empty()) {
        prepared.valid = !program.empty();
        return prepared;
    }

    // All nodes execute normally until the surface authority can publish real
    // baked pixels and GraphExecutor can bind them as the island root output.
    // Keeping the mask empty (rather than all-false) also makes the absence of
    // a prepared skip program explicit to the executor.
    prepared.valid = true;
    return prepared;
}
