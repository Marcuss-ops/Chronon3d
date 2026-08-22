// ──────────────────────────────────────────────────────────────────────────────
// src/render_graph/compiler/compiled_template_program.cpp
// Fase A (TICKET-VIDEO-COMPILER-ARCH-V1) — derive CompiledTemplateProgram
// from a CompiledFrameGraph.  Single source of truth: shared_ptr to the
// compiled graph; template-level metadata is lifted once at compile time.
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/render_graph/compiler/compiled_template_program.hpp>

#include <algorithm>  // std::max

namespace chronon3d::graph {

namespace {

// ── Temporal classification rank ────────────────────────────────────────────
// Static < TransformDynamic < ParameterDynamic < ContentDynamic < ExternalDynamic
[[nodiscard]] constexpr int temporal_rank(TemporalClass tc) noexcept {
    switch (tc) {
        case TemporalClass::Pure:             return 0;
        case TemporalClass::Static:           return 0;
        case TemporalClass::TransformDynamic: return 1;
        case TemporalClass::ParameterDynamic: return 2;
        case TemporalClass::TimeDependent:    return 2;  // generic per-frame
        case TemporalClass::ContentDynamic:   return 3;
        case TemporalClass::Stateful:         return 3;  // content version
        case TemporalClass::ExternalDynamic:  return 4;
    }
    return 4;
}

[[nodiscard]] constexpr TemporalClass rank_to_class(int rank) noexcept {
    if (rank <= 0) return TemporalClass::Static;
    if (rank == 1) return TemporalClass::TransformDynamic;
    if (rank == 2) return TemporalClass::ParameterDynamic;
    if (rank == 3) return TemporalClass::ContentDynamic;
    return TemporalClass::ExternalDynamic;
}

/// Derive the own-state TemporalCapabilities of a node from its kind,
/// shape payload and cache policy.  The cache policy is the strongest
/// signal (FrameInvariantMemory ⇒ static; FrameVariant ⇒ per-frame).
[[nodiscard]] TemporalCapabilities derive_capabilities(
    const CompiledNodeInfo& node) noexcept {
    TemporalCapabilities caps;

    switch (node.kind) {
        case RenderGraphNodeKind::Video:
            caps.depends_on_external_frame = true;
            break;
        case RenderGraphNodeKind::TextRun:
            caps.content_depends_on_time = true;
            break;
        case RenderGraphNodeKind::Transform:
            caps.transform_depends_on_time = true;
            break;
        case RenderGraphNodeKind::Effect:
        case RenderGraphNodeKind::ColorConvert:
            caps.parameters_depend_on_time = true;
            break;
        case RenderGraphNodeKind::MotionBlur:
            caps.content_depends_on_time = true;
            break;
        case RenderGraphNodeKind::Source:
            // ShapeType::Image == 7 → authored asset, static by default;
            // any other source payload is content-authored per frame.
            if (node.shape_type != 7) {
                caps.content_depends_on_time = true;
            }
            break;
        default:
            // Composite / Mask / Output / Precomp / Transition / Adjustment:
            // derived from inputs (no own time dependency).
            break;
    }

    // Cache policy refines the kind-derived caps.
    // - FrameInvariantMemory → forced static (all caps zeroed).
    // - Disabled → conservative: treat as ExternalDynamic.
    // - FrameVariant (default) → keep kind-derived caps unchanged
    //   (the kind already signals the correct dependency class).
    const auto policy = node.cache_policy;
    if (policy.reusable_across_frames()) {
        caps = TemporalCapabilities{};  // forced static
    } else if (!policy.enabled()) {
        caps = TemporalCapabilities{};
        caps.depends_on_external_frame = true;  // conservative
    }

    return caps;
}

[[nodiscard]] TemporalClass caps_to_class(const TemporalCapabilities& caps) noexcept {
    if (caps.fully_static()) return TemporalClass::Static;
    if (caps.depends_on_external_frame) return TemporalClass::ExternalDynamic;
    if (caps.content_depends_on_time) return TemporalClass::ContentDynamic;
    if (caps.parameters_depend_on_time) return TemporalClass::ParameterDynamic;
    if (caps.transform_depends_on_time) return TemporalClass::TransformDynamic;
    return TemporalClass::Static;
}

/// FNV-1a fingerprint over a deterministic member set (stable node ids).
[[nodiscard]] std::uint64_t region_fingerprint(
    const std::vector<GraphNodeId>& members,
    const CompiledFrameGraph& compiled) noexcept {
    std::uint64_t h = 1469598103934665603ULL;  // FNV-1a offset basis
    auto combine = [&h](std::uint64_t v) noexcept {
        h ^= v;
        h *= 1099511628211ULL;
    };
    for (const auto id : members) {
        combine(id);  // node id is deterministic in the compiled graph
        if (id < compiled.nodes.size()) {
            combine(compiled.nodes[id].stable_node_id.value);
            combine(static_cast<std::uint64_t>(compiled.nodes[id].kind));
        }
    }
    // Guard against the null fingerprint (0).
    return h == 0 ? 1 : h;
}

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

} // anonymous namespace

TemporalAnalysisResult
classify_temporal(const CompiledFrameGraph& compiled) {
    TemporalAnalysisResult result;
    result.total_count = compiled.nodes.size();
    result.per_node.resize(compiled.nodes.size());

    // Execution levels are topological; propagate front-to-back.
    // A node's final class = max(own class, max(input classes)).
    for (const auto& level : compiled.levels) {
        for (const auto id : level) {
            if (id >= compiled.nodes.size()) continue;
            const auto& node = compiled.nodes[id];

            int rank = temporal_rank(caps_to_class(derive_capabilities(node)));
            for (const auto in : node.inputs) {
                if (in >= result.per_node.size()) continue;
                rank = std::max(rank, temporal_rank(result.per_node[in].classification));
            }

            result.per_node[id] = CompiledTemporalInfo{
                .node           = id,
                .classification = rank_to_class(rank),
            };
            if (is_static(result.per_node[id].classification)) {
                ++result.static_count;
            }
        }
    }

    return result;
}

std::vector<StaticBakeRegion> bake_maximal_static_islands(
    const CompiledFrameGraph& compiled,
    const TemporalAnalysisResult& temporal) {
    std::vector<StaticBakeRegion> regions;

    if (compiled.nodes.empty() || temporal.per_node.size() != compiled.nodes.size()) {
        return regions;
    }

    // Region assignment: every static node gets a region id; a dynamic node
    // ends the region.  Members accumulate in level order so the vector is
    // already topological.
    std::vector<int> region_of(compiled.nodes.size(), -1);

    for (const auto& level : compiled.levels) {
        for (const auto id : level) {
            if (id >= compiled.nodes.size()) continue;
            const auto cls = temporal.classification(id);
            if (!is_static(cls)) continue;  // dynamic node: no region

            // Reuse the region of the first static input, if any.
            int region = -1;
            for (const auto in : compiled.nodes[id].inputs) {
                if (in < region_of.size() && region_of[in] >= 0) {
                    region = region_of[in];
                    break;
                }
            }
            if (region < 0) {
                region = static_cast<int>(regions.size());
                regions.push_back(StaticBakeRegion{});
                regions.back().root = id;
            }
            region_of[id] = region;
            regions[static_cast<std::size_t>(region)].members.push_back(id);
        }
    }

    // Populate bake ids + fingerprints for every discovered region.
    for (std::size_t i = 0; i < regions.size(); ++i) {
        auto& region = regions[i];
        region.bake_id     = static_cast<std::uint32_t>(i);
        region.fingerprint = region_fingerprint(region.members, compiled);
        region.is_baked    = !region.members.empty();
    }

    return regions;
}

std::vector<StaticBakeRegion> merge_contiguous_static_regions(
    std::vector<StaticBakeRegion> regions,
    const CompiledFrameGraph& compiled,
    const TemporalAnalysisResult& temporal) {
    // Fase B first pass: regions are already maximal per connected static
    // component (a dynamic node between two static runs splits them, which is
    // correct — merging across a dynamic separator would break baking).  The
    // merge pass exists for later phases where a static island may be
    // fragmented by execution order; today it is a semantic no-op that
    // re-validates determinism.
    (void)compiled;
    (void)temporal;
    return regions;
}

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

    return prog;
}

} // namespace chronon3d::graph