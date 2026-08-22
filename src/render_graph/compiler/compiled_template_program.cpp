// ──────────────────────────────────────────────────────────────────────────────
// src/render_graph/compiler/compiled_template_program.cpp
// Fase A (TICKET-VIDEO-COMPILER-ARCH-V1) — derive CompiledTemplateProgram
// from a CompiledFrameGraph.  Single source of truth: shared_ptr to the
// compiled graph; template-level metadata is lifted once at compile time.
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/render_graph/compiler/compiled_template_program.hpp>

namespace chronon3d::graph {

namespace {

ResourceKind classify_binding_kind(RenderGraphNodeKind node_kind,
                                   int shape_type) noexcept {
    switch (node_kind) {
        case RenderGraphNodeKind::TextRun:
            return ResourceKind::Text;
        case RenderGraphNodeKind::Video:
            return ResourceKind::Video;
        case RenderGraphNodeKind::Source:
            // ShapeType::Image == 7 (canonical value from ShapeType enum)
            if (shape_type == 7) return ResourceKind::Image;
            return ResourceKind::Other;
        default:
            break;
    }
    return ResourceKind::Other;
}

ParameterSchema build_parameter_schema(const CompiledFrameGraph& compiled) {
    ParameterSchema schema;

    // ── From parameter ring (operations) ───────────────────────────────
    for (const auto& op : compiled.program.operations) {
        if (op.parameter_size == 0) continue;
        const auto node_id = op.node;
        if (node_id >= compiled.nodes.size()) continue;
        const auto& node_info = compiled.nodes[node_id];
        schema.entries.push_back(ParameterSchemaEntry{
            .node             = node_id,
            .stable_node      = node_info.stable_node_id,
            .parameter_offset = op.parameter_offset,
            .parameter_size   = op.parameter_size,
        });
        schema.total_bytes += op.parameter_size;
    }

    // ── From prepared parameters (frame count) ─────────────────────────
    schema.has_prepared_parameters =
        compiled.prepared_parameters != nullptr;
    if (schema.has_prepared_parameters) {
        schema.frame_count =
            compiled.prepared_parameters->frame_count();
    }

    // ── From execution summary ─────────────────────────────────────────
    schema.fully_recorded = compiled.program.fully_recorded;

    return schema;
}

ResourceManifest build_resource_manifest(const CompiledFrameGraph& compiled) {
    ResourceManifest manifest;

    for (const auto& node_info : compiled.nodes) {
        const auto& bm = node_info.binding_meta;
        if (!bm.active) continue;

        ResourceManifestEntry entry;
        entry.kind       = classify_binding_kind(node_info.kind, node_info.shape_type);
        entry.binding_id = node_info.layer_id;
        entry.node       = node_info.id;
        manifest.entries.push_back(std::move(entry));
    }

    return manifest;
}

std::vector<StaticBakeRegion> build_static_regions(
    const CompiledFrameGraph& compiled) {
    std::vector<StaticBakeRegion> regions;
    regions.reserve(compiled.program.static_bakes.size());

    for (const auto& bake : compiled.program.static_bakes) {
        StaticBakeRegion region;
        region.bake_id     = bake.persistent_surface_handle;
        region.root        = bake.root_node;
        // Fase A: members empty (Phase C fills via static island discovery)
        region.fingerprint = bake.static_fingerprint;
        region.is_baked    = bake.is_baked;
        regions.push_back(std::move(region));
    }

    return regions;
}

} // anonymous namespace

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

    // ── Schema / Manifest / Regions / Batches / Boundaries ─────────────
    prog.parameters    = build_parameter_schema(*prog.compiled);
    prog.resources     = build_resource_manifest(*prog.compiled);
    prog.static_regions = build_static_regions(*prog.compiled);

    // Batches: lift layer_batches as CompiledGpuBatch (alias in Fase A)
    prog.batches.assign(
        prog.compiled->program.layer_batches.begin(),
        prog.compiled->program.layer_batches.end());

    // Boundaries: empty in Fase A (Phase G fills)
    prog.boundaries.clear();

    prog.valid = prog.compiled->valid;

    return prog;
}

} // namespace chronon3d::graph