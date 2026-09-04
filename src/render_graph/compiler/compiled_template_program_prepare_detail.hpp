// ── Phase 4 — prepare() bakes maximal static islands ────────────────────────
//
// For every StaticBakeRegion discovered by the temporal analysis, prepare()
// marks interior nodes as skip and records the root node as a baked producer.
//
// Phase 4 does NOT allocate GPU surfaces — surface residency lands in Phase 5
// (PhysicalFramebufferPlan materialization).  What Phase 4 delivers:
//   1. `interior_node_skip` mask: interior nodes execute = 0 after prepare
//   2. `baked_regions`: per-region metadata (root, members, fingerprint)
//   3. The contract that render() never re-evaluates static islands.
//
// When mixed with m_cached_result (per-node static caching, Phase 4 cleanup),
// the per-node cache becomes redundant — the region-level bake covers the
// same static surface, and the skip mask eliminates the execute call entirely
// instead of returning a cached Framebuffer on each frame.
void preallocate_surfaces(const CompiledTemplateProgram& program,
                          RenderBackend* backend,
                          std::uint32_t canvas_width,
                          std::uint32_t canvas_height) {
    if (!backend || !program.compiled || canvas_width == 0 || canvas_height == 0) {
        return;
    }
    const auto& plan = program.compiled->resource_table();
    if (plan.empty()) return;

    backend->preallocate_plan_surfaces(canvas_width, canvas_height, plan);
}

PreparedFrameProgram prepare(const CompiledTemplateProgram& program) {
    PreparedFrameProgram prepared;
    if (!program.compiled || program.static_regions.empty()) {
        prepared.valid = !program.empty();
        return prepared;
    }

    const auto& compiled = *program.compiled;
    const auto node_count = compiled.nodes.size();

    // Every node starts NOT skipped (executed normally).
    prepared.interior_node_skip.assign(node_count, false);

    for (const auto& region : program.static_regions) {
        if (region.members.empty()) continue;

        PreparedStaticBake bake;
        bake.bake_id = region.bake_id;
        bake.root = region.root;

        // All members EXCEPT the root are interior nodes: they are fully
        // skipped during frame execution.  The root is the source whose
        // output represents the entire baked island.
        for (GraphNodeId member : region.members) {
            if (member == region.root) continue;
            if (member < node_count) {
                prepared.interior_node_skip[member] = true;
                bake.interior_nodes.push_back(member);
                ++prepared.skipped_interior_nodes;
            }
        }

        // The root node's surface handle will be wired by the physical
        // resource plan (Phase 5).  For now, mark the bake as valid.
        prepared.baked_regions[region.bake_id] = std::move(bake);
    }

    prepared.valid = true;
    return prepared;
}
