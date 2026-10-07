#include <chronon3d/core/hash/hash_builder.hpp>

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

/// Fingerprint the temporal dependency graph independently of seek/render
/// order. Stable IDs and sorted edges are used rather than transient graph
/// addresses; cache policy and derived class participate in invalidation.
[[nodiscard]] std::uint64_t temporal_dependency_fingerprint(
    const CompiledFrameGraph& compiled,
    const TemporalAnalysisResult& temporal) {
    core::hash::HashBuilder hash;
    hash.add("chronon.temporal-dependencies.v1");
    hash.add(static_cast<std::uint64_t>(compiled.nodes.size()));
    std::vector<const CompiledNodeInfo*> nodes;
    nodes.reserve(compiled.nodes.size());
    for (const auto& node : compiled.nodes) nodes.push_back(&node);
    std::sort(nodes.begin(), nodes.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->stable_node_id.value < rhs->stable_node_id.value;
    });
    for (const auto* node : nodes) {
        hash.add(node->stable_node_id.value);
        hash.add_enum(node->kind);
        hash.add(node->shape_type);
        hash.add_enum(node->cache_policy.mode);
        hash.add_enum(node->cache_policy.invalidation);
        hash.add_enum(node->cache_policy.temporal_class);
        hash.add(node->cache_policy.reason);
        const auto classification = node->id < temporal.per_node.size()
            ? temporal.per_node[node->id].classification
            : TemporalClass::ExternalDynamic;
        hash.add_enum(classification);
        std::vector<std::uint64_t> inputs;
        inputs.reserve(node->inputs.size());
        for (const auto input : node->inputs) {
            inputs.push_back(input < compiled.nodes.size()
                ? compiled.nodes[input].stable_node_id.value : std::uint64_t{0});
        }
        std::sort(inputs.begin(), inputs.end());
        hash.add(static_cast<std::uint64_t>(inputs.size()));
        for (const auto input : inputs) hash.add(input);
    }
    const auto result = hash.finish();
    return result == 0 ? 1 : result;
}

/// FNV-compatible stable candidate-region fingerprint. The candidate is
/// metadata, not proof that pixels have been baked.
[[nodiscard]] std::uint64_t region_fingerprint(
    const std::vector<GraphNodeId>& members,
    const CompiledFrameGraph& compiled) {
    core::hash::HashBuilder hash;
    hash.add("chronon.static-island-candidate.v1");
    auto ordered_members = members;
    std::sort(ordered_members.begin(), ordered_members.end(),
              [&compiled](GraphNodeId lhs, GraphNodeId rhs) {
                  const auto left = lhs < compiled.nodes.size()
                      ? compiled.nodes[lhs].stable_node_id.value : 0;
                  const auto right = rhs < compiled.nodes.size()
                      ? compiled.nodes[rhs].stable_node_id.value : 0;
                  return left == right ? lhs < rhs : left < right;
              });
    for (const auto id : ordered_members) {
        if (id >= compiled.nodes.size()) {
            hash.add(std::uint64_t{0});
            continue;
        }
        const auto& node = compiled.nodes[id];
        hash.add(node.stable_node_id.value);
        hash.add_enum(node.kind);
        hash.add(node.shape_type);
        hash.add_enum(node.cache_policy.mode);
        hash.add_enum(node.cache_policy.invalidation);
        hash.add(node.cache_policy.reason);
        std::vector<std::uint64_t> inputs;
        inputs.reserve(node.inputs.size());
        for (const auto input : node.inputs) {
            inputs.push_back(input < compiled.nodes.size()
                ? compiled.nodes[input].stable_node_id.value : std::uint64_t{0});
        }
        std::sort(inputs.begin(), inputs.end());
        hash.add(static_cast<std::uint64_t>(inputs.size()));
        for (const auto input : inputs) hash.add(input);
    }
    const auto result = hash.finish();
    return result == 0 ? 1 : result;
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
