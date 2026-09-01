CompiledTemplateProgram
compile_template_program(CompiledFrameGraph compiled_arg) {
    // ── Move into shared ownership ─────────────────────────────────────
    auto owned = std::make_shared<CompiledFrameGraph>(
        std::move(compiled_arg));

    CompiledTemplateProgram prog;
    prog.compiled = std::move(owned);

    // ── Fingerprint ────────────────────────────────────────────────────
    prog.fingerprint.topology_hash  = prog.compiled->structure_hash;
    prog.fingerprint.renderer_abi   = kRenderAbiV1;
    prog.fingerprint.quality_profile = kQualityProfileDefault;

    // ── Fase B: temporal analysis + maximal static islands ───────────
    prog.temporal = classify_temporal(*prog.compiled);

    // ── Fase D: parameter ring (triple-buffered default) ──────────────
    prog.param_ring = build_parameter_ring(*prog.compiled, /*slot_count=*/3);

    // ── Fase E: command replay bridge ─────────────────────────────────
    prog.replay = build_command_replay(prog.param_ring, /*slot_count=*/3);

    // ── Schema / Manifest / Regions / Batches / Boundaries ─────────────
    prog.parameters    = build_parameter_schema(*prog.compiled);
    prog.resources     = build_resource_manifest(*prog.compiled);
    prog.static_regions = merge_contiguous_static_regions(
        bake_maximal_static_islands(*prog.compiled, prog.temporal),
        *prog.compiled,
        prog.temporal);

    // Batches: lift layer_batches as CompiledGpuBatch (alias in Fase A)
    prog.batches.assign(
        prog.compiled->program.layer_batches.begin(),
        prog.compiled->program.layer_batches.end());

    // Boundaries: empty in Fase A (Phase G fills)
    prog.boundaries.clear();

    prog.valid = prog.compiled->valid;

    // ── Phase 4 — bake static islands into compiled program ─────────
    // Populate interior_node_skip so the executor skips baked interior
    // nodes on every frame.  In the full prepare() lifecycle this merge
    // moves to the pipeline; for now auto-wire at compile time so all
    // existing callers benefit without changing the executor interface.
    if (prog.valid) {
        auto prepared = prepare(prog);
        if (prepared.valid && !prepared.interior_node_skip.empty()) {
            // The compiled graph is const-shared; mutate the program
            // inside (non-const access through shared_ptr).  This is
            // safe because the compiler is the sole writer before the
            // executor reads.
            auto& mutable_program =
                const_cast<CompiledFrameProgram&>(prog.compiled->program);
            mutable_program.interior_node_skip =
                std::move(prepared.interior_node_skip);
        }
    }

    return prog;
}
