void FrameGraphCompiler::build_node_metadata(
    RenderGraph& graph,
    RenderGraphContext& ctx,
    CompiledFrameGraph& compiled,
    const FrameGraphCompileOptions& options
) const {
    const size_t node_count = graph.size();
    compiled.nodes.resize(node_count);

    // Capture processor ownership once at the compiler boundary. Real
    // SoftwareBackend instances provide an immutable owning snapshot. A
    // backend that participates in shape/effect compilation without one is
    // rejected at the first binding request; non-renderable graph operators
    // remain compilable without a backend.
    compiled.processor_snapshot = ctx.services.backend
        ? ctx.services.backend->processor_snapshot()
        : nullptr;
    // Standalone graph callers may provide a backend without going through
    // scene_context_setup(), leaving the optional context generation at its
    // zero sentinel. In that case the owning backend snapshot is the
    // canonical registry generation; use it rather than rejecting a valid
    // graph at the compiler boundary.
    if (ctx.services.registry_generation == 0 && compiled.processor_snapshot) {
        ctx.services.registry_generation = compiled.processor_snapshot->generation();
    }
    compiled.registry_generation = ctx.services.registry_generation;
    compiled.processor_snapshot_identity = compiled.processor_snapshot
        ? compiled.processor_snapshot->identity()
        : 0;
    if (compiled.processor_snapshot &&
        compiled.processor_snapshot->generation() != compiled.registry_generation) {
        throw std::runtime_error(
            "FrameGraphCompiler: processor snapshot generation does not match "
            "RenderGraphContext registry generation");
    }

    const auto require_snapshot = [&]() -> const renderer::ProcessorRegistrySnapshot& {
        if (!compiled.processor_snapshot) {
            throw std::runtime_error(
                "FrameGraphCompiler: backend must provide an owning processor snapshot");
        }
        return *compiled.processor_snapshot;
    };
    const bool requires_snapshot = ctx.services.backend &&
        ctx.services.backend->requires_processor_snapshot();
    const auto resolve_shape = [&](ShapeType type) {
        return require_snapshot().shape_handle(type);
    };
    const auto resolve_effect = [&](std::type_index type) {
        return require_snapshot().effect_handle(type);
    };
    std::vector<char> reachable(node_count, 0);
    for (const auto& level : compiled.levels) {
        for (GraphNodeId id : level) {
            reachable[id] = 1;
        }
    }

    std::vector<std::vector<GraphNodeId>> children(node_count);
    for (GraphNodeId child = 0; child < node_count; ++child) {
        if (!reachable[child]) continue;
        for (GraphNodeId parent : graph.inputs(child)) {
            if (!reachable[parent]) continue;
            children[parent].push_back(child);
        }
    }

    for (GraphNodeId id = 0; id < node_count; ++id) {
        auto& node_info = compiled.nodes[id];
        node_info.id = id;
        if (graph.has_node(id)) {
            auto& node = graph.node(id);
            node_info.name = node.name();
            node_info.layer_id = node.layer_id();
            node_info.kind = node.kind();
            node_info.inputs = graph.inputs(id);
            if (reachable[id]) {
                node_info.reachable = true;
                node_info.consumers = children[id];

                // Carry the builder's canonical layer location into the
                // compiled program.  The binding table used to depend on
                // metadata that no production builder ever populated, so
                // every real precomp program ended up with zero bindings and
                // its per-frame refresh path was silently skipped.
                const bool refreshable_kind =
                    node_info.kind == RenderGraphNodeKind::Source ||
                    node_info.kind == RenderGraphNodeKind::TextRun ||
                    node_info.kind == RenderGraphNodeKind::Transform ||
                    node_info.kind == RenderGraphNodeKind::Effect;
                const bool has_layer_location = node.layer_index() != UINT32_MAX;
                const bool is_root_source =
                    !has_layer_location && node.layer_id().empty() &&
                    (node_info.kind == RenderGraphNodeKind::Source ||
                     node_info.kind == RenderGraphNodeKind::TextRun);
                if (refreshable_kind && (has_layer_location || is_root_source)) {
                    node_info.binding_meta = SceneBindingMetadata{
                        true,
                        node.layer_index(),
                        node.item_index(),
                        0,
                        0,
                    };
                }

                node_info.cache_policy = node.cache_policy();

                // Capture structural payload discriminators once at compile
                // time. Refresh may replace dynamic content, matrices and
                // cache keys, but it must never change a node's render kind
                // or shape topology in place.
                if (const auto* source = dynamic_cast<const SourceNode*>(&node)) {
                    node_info.processor_id = "source:" +
                        std::to_string(static_cast<int>(source->render_node().shape.type()));
                    node_info.shape_type = static_cast<int>(source->render_node().shape.type());
                    node_info.shape_processor = requires_snapshot
                        ? resolve_shape(source->render_node().shape.type())
                        : renderer::ShapeProcessorHandle{};
                    if (requires_snapshot && !node_info.shape_processor.valid()) {
                        throw std::runtime_error(
                            "FrameGraphCompiler: missing compiled shape processor for node '" +
                            std::string(node.name()) + "'");
                    }
                    node_info.shape_processors_offset = static_cast<std::uint32_t>(
                        compiled.shape_processor_table.size());
                    node_info.shape_processors_count = 1;
                    compiled.shape_processor_table.push_back(node_info.shape_processor);
                } else if (const auto* multi = dynamic_cast<const MultiSourceNode*>(&node)) {
                    node_info.processor_id = "multi_source";
                    node_info.shape_type = -2;
                    node_info.source_shape_types.reserve(multi->items().size());
                    node_info.shape_processors_offset = static_cast<std::uint32_t>(
                        compiled.shape_processor_table.size());
                    node_info.shape_processors_count = static_cast<std::uint32_t>(
                        multi->items().size());
                    for (const auto& item : multi->items()) {
                        if (!item.node) {
                            throw std::runtime_error(
                                "FrameGraphCompiler: multi-source node '" +
                                std::string(node.name()) +
                                "' contains a null renderable item");
                        }
                        node_info.source_shape_types.push_back(
                            static_cast<int>(item.node->shape.type()));
                        if (item.node->shape.type() == ShapeType::TextRun) {
                            compiled.shape_processor_table.push_back(renderer::ShapeProcessorHandle{});
                        } else {
                            const auto handle = requires_snapshot
                                ? resolve_shape(item.node->shape.type())
                                : renderer::ShapeProcessorHandle{};
                            if (requires_snapshot && !handle.valid()) {
                                throw std::runtime_error(
                                    "FrameGraphCompiler: missing compiled shape processor for multi-source node '" +
                                    std::string(node.name()) + "'");
                            }
                            compiled.shape_processor_table.push_back(handle);
                        }
                    }
                } else if (const auto* text = dynamic_cast<const TextRunNode*>(&node)) {
                    node_info.processor_id = "text_run";
                    node_info.shape_type = static_cast<int>(text->render_node().shape.type());
                } else if (const auto* effect = dynamic_cast<const EffectStackNode*>(&node)) {
                    node_info.processor_id = "effect_stack";
                    node_info.effect_processors_offset = static_cast<std::uint32_t>(
                        compiled.effect_processor_table.size());
                    node_info.effect_processors_count = static_cast<std::uint32_t>(
                        effect->effects().size());
                    for (const auto& instance : effect->effects()) {
                        const auto handle = instance.enabled && requires_snapshot
                            ? resolve_effect(instance.param_type_index())
                            : renderer::EffectProcessorHandle{};
                        if (instance.enabled && requires_snapshot && !handle.valid()) {
                            throw std::runtime_error(
                                "FrameGraphCompiler: missing compiled effect processor for node '" +
                                std::string(node.name()) + "'");
                        }
                        compiled.effect_processor_table.push_back(handle);
                    }
                } else if (const auto* adjustment = dynamic_cast<const AdjustmentNode*>(&node)) {
                    node_info.processor_id = "adjustment";
                    node_info.effect_processors_offset = static_cast<std::uint32_t>(
                        compiled.effect_processor_table.size());
                    node_info.effect_processors_count = static_cast<std::uint32_t>(
                        adjustment->effects().size());
                    for (const auto& instance : adjustment->effects()) {
                        const auto handle = instance.enabled && requires_snapshot
                            ? resolve_effect(instance.param_type_index())
                            : renderer::EffectProcessorHandle{};
                        if (instance.enabled && requires_snapshot && !handle.valid()) {
                            throw std::runtime_error(
                                "FrameGraphCompiler: missing compiled effect processor for node '" +
                                std::string(node.name()) + "'");
                        }
                        compiled.effect_processor_table.push_back(handle);
                    }
                } else if (dynamic_cast<const DofEffectNode*>(&node)) {
                    node_info.processor_id = "dof";
                    const auto handle = requires_snapshot
                        ? resolve_effect(std::type_index(typeid(BlurParams)))
                        : renderer::EffectProcessorHandle{};
                    if (requires_snapshot && !handle.valid()) {
                        throw std::runtime_error(
                            "FrameGraphCompiler: missing compiled effect processor for node '" +
                            std::string(node.name()) + "'");
                    }
                    node_info.effect_processors_offset = static_cast<std::uint32_t>(
                        compiled.effect_processor_table.size());
                    node_info.effect_processors_count = 1;
                    compiled.effect_processor_table.push_back(handle);
                } else {
                    node_info.processor_id = std::string(to_string(node_info.kind));
                }

                if (id < ctx.node_exec.early_exit_skip.size() && ctx.node_exec.early_exit_skip[id]) {
                    node_info.early_exit_skip = true;
                }
                if (options.compute_bboxes) {
                    node_info.predicted_bbox = node.predicted_bbox(ctx);
                }

                // ── Work Package 4 — derive stable_node_id ──
                const std::uint64_t layer_id_hash =
                    hash_string(node_info.layer_id);
                const std::uint64_t kind_and_name_hash = hash_combine(
                    static_cast<std::uint64_t>(node_info.kind),
                    hash_string(node_info.name)
                );
                node_info.stable_node_id = hash_stable_node_inputs(
                    layer_id_hash, kind_and_name_hash);
            }
        }
    }

    // ── Work Package 4.4 — input-aware refinement (Merkle-style) ────────
    // Resolve from sinks toward sources so consumer identities already carry
    // their own downstream context when they are folded into a producer.
    for (auto level_it = compiled.levels.rbegin();
         level_it != compiled.levels.rend(); ++level_it) {
        for (GraphNodeId id : *level_it) {
            if (id >= node_count) continue;
            auto& node_info = compiled.nodes[id];
            if (!node_info.reachable) continue;
            if (node_info.stable_node_id == kInvalidStableNodeId) continue;

            uint64_t h = node_info.stable_node_id.value;
            bool folded = false;

            std::vector<uint64_t> input_sids;
            input_sids.reserve(node_info.inputs.size());
            for (GraphNodeId input_id : node_info.inputs) {
                if (input_id >= node_count) continue;
                const auto& input_info = compiled.nodes[input_id];
                if (!input_info.reachable) continue;
                if (input_info.stable_node_id == kInvalidStableNodeId) continue;
                input_sids.push_back(input_info.stable_node_id.value);
            }
            std::sort(input_sids.begin(), input_sids.end());
            // Include both sides of the local graph neighbourhood.  A source
            // node can be materialised more than once for a derived pass
            // (for example, a shadow caster): those instances may have the
            // same layer/name/kind and no inputs, but they feed different
            // consumers.  Folding sorted consumer identities keeps the ID
            // content-derived without using the volatile graph-node index.
            std::vector<uint64_t> consumer_sids;
            consumer_sids.reserve(children[id].size());
            for (GraphNodeId child_id : children[id]) {
                if (child_id >= node_count) continue;
                const auto& child_info = compiled.nodes[child_id];
                if (!child_info.reachable ||
                    child_info.stable_node_id == kInvalidStableNodeId) {
                    continue;
                }
                consumer_sids.push_back(child_info.stable_node_id.value);
            }
            std::sort(consumer_sids.begin(), consumer_sids.end());

            constexpr uint64_t kInputDomain = 0x494e505554ULL;    // INPUT
            constexpr uint64_t kConsumerDomain = 0x434f4e53554d4552ULL; // CONSUMER
            h ^= kInputDomain;
            h *= 0x100000001b3ULL;
            h ^= static_cast<uint64_t>(input_sids.size());
            h *= 0x100000001b3ULL;
            for (uint64_t sid : input_sids) {
                h ^= sid;
                h *= 0x100000001b3ULL;
                folded = true;
            }
            h ^= kConsumerDomain;
            h *= 0x100000001b3ULL;
            h ^= static_cast<uint64_t>(consumer_sids.size());
            h *= 0x100000001b3ULL;
            for (uint64_t sid : consumer_sids) {
                h ^= sid;
                h *= 0x100000001b3ULL;
                folded = true;
            }
            if (folded) {
                node_info.stable_node_id = StableNodeId{h == 0u ? 1u : h};
            }
        }
    }

    // ── Work Package 4 — collision detection ─────────────────────────────
    {
        std::unordered_map<StableNodeId, GraphNodeId> seen;
        for (size_t i = 0; i < compiled.nodes.size(); ++i) {
            if (!compiled.nodes[i].reachable) continue;
            const auto sid = compiled.nodes[i].stable_node_id;
            if (sid == kInvalidStableNodeId) continue;
            auto [it, inserted] = seen.emplace(sid, static_cast<GraphNodeId>(i));
            if (!inserted) {
                const auto describe_consumers = [&](GraphNodeId node_id) {
                    std::string text;
                    for (GraphNodeId child_id : compiled.nodes[node_id].consumers) {
                        if (!text.empty()) text += ",";
                        text += std::to_string(child_id);
                        if (child_id < compiled.nodes.size()) {
                            text += ":" + compiled.nodes[child_id].name;
                        }
                    }
                    return text.empty() ? std::string{"-"} : text;
                };
                throw std::runtime_error(
                    "FrameGraphCompiler: stable_node_id collision between nodes "
                    + std::to_string(it->second) + " (layer='"
                    + compiled.nodes[it->second].layer_id + "', name='"
                    + compiled.nodes[it->second].name + "', kind="
                    + std::to_string(static_cast<int>(compiled.nodes[it->second].kind))
                    + ") and " + std::to_string(i) + " (layer='"
                    + compiled.nodes[i].layer_id + "', name='"
                    + compiled.nodes[i].name + "', kind="
                    + std::to_string(static_cast<int>(compiled.nodes[i].kind))
                    + ", consumers=" + describe_consumers(i) + ")"
                    + "; first_consumers=" + describe_consumers(it->second));
            }
        }
    }
}
