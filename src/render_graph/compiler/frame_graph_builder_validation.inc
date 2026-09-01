namespace {

// The graph taxonomy deliberately keeps non-shape nodes out of the shape
// processor boundary. Null/Group/Control are not RenderGraphNodeKind values
// in this repository; TextRun has its own processor path, Image is an
// ordinary renderable ShapeType, and Transition is a graph node that operates
// on framebuffer inputs. Only SourceNode/MultiSourceNode payloads enter this
// validator, so a non-shape node can never be rejected as ShapeType::None.
[[nodiscard]] bool uses_shape_processor(const RenderGraphNode& node) noexcept {
    return dynamic_cast<const SourceNode*>(&node) != nullptr ||
           dynamic_cast<const MultiSourceNode*>(&node) != nullptr;
}

} // namespace

void FrameGraphCompiler::validate_renderable_shape(
    const ::chronon3d::RenderNode& render_node,
    const CompiledNodeInfo& node_info,
    const RenderGraphContext& ctx
) const {
    const auto shape_type = render_node.shape.type();
    if (shape_type == ShapeType::None) {
        throw std::runtime_error(
            "FrameGraphCompiler: renderable Shape node '" +
            node_info.name + "' has ShapeType::None and cannot reach a shape processor");
    }

    if (!ctx.services.backend) {
        throw std::runtime_error(
            "FrameGraphCompiler: renderable Shape node '" +
            node_info.name + "' has no render backend");
    }
    if (const auto error = ctx.services.backend->validate_render_node(render_node)) {
        throw std::runtime_error(
            "FrameGraphCompiler: renderable Shape node '" +
            node_info.name + "' is invalid: " + error->message);
    }
}

void FrameGraphCompiler::validate_renderable_graph(
    const RenderGraph& graph,
    GraphNodeId output,
    const RenderGraphContext& ctx
) const {
    const size_t node_count = graph.size();
    std::vector<char> reachable(node_count, 0);
    std::vector<GraphNodeId> stack{output};
    while (!stack.empty()) {
        const GraphNodeId id = stack.back();
        stack.pop_back();
        if (id >= node_count || reachable[id]) {
            continue;
        }
        reachable[id] = 1;
        for (const GraphNodeId parent : graph.inputs(id)) {
            stack.push_back(parent);
        }
    }

    for (GraphNodeId id = 0; id < node_count; ++id) {
        if (!reachable[id] || !graph.has_node(id)) {
            continue;
        }
        const auto& node = graph.node(id);
        if (!uses_shape_processor(node)) {
            // TextRunNode, TransitionNode, and the remaining graph operators
            // have dedicated processor/input paths. They must not be forced
            // through ShapeType validation; Null/Group/Control are likewise
            // absent from this graph taxonomy rather than represented by a
            // fake ShapeType::None placeholder.
            continue;
        }
        if (const auto* source = dynamic_cast<const SourceNode*>(&node)) {
            CompiledNodeInfo info;
            info.id = id;
            info.name = node.name();
            validate_renderable_shape(source->render_node(), info, ctx);
        } else if (const auto* multi = dynamic_cast<const MultiSourceNode*>(&node)) {
            CompiledNodeInfo info;
            info.id = id;
            info.name = node.name();
            for (const auto& item : multi->items()) {
                if (!item.node) {
                    throw std::runtime_error(
                        "FrameGraphCompiler: multi-source node '" +
                        std::string(node.name()) +
                        "' contains a null renderable item");
                }
                validate_renderable_shape(*item.node, info, ctx);
            }
        }
    }
}

void FrameGraphCompiler::compute_resource_lifetimes(
    CompiledFrameGraph& compiled
) const {
    const size_t node_count = compiled.graph.size();
    compiled.lifetimes.assign(node_count, ResourceLifetime{});

    for (size_t level_index = 0; level_index < compiled.levels.size(); ++level_index) {
        for (GraphNodeId node_id : compiled.levels[level_index]) {
            if (node_id < node_count) {
                compiled.lifetimes[node_id].producer = node_id;
                compiled.lifetimes[node_id].first_level = level_index;
                compiled.lifetimes[node_id].last_level = level_index;
            }
        }
    }

    for (size_t level_index = 0; level_index < compiled.levels.size(); ++level_index) {
        for (GraphNodeId node_id : compiled.levels[level_index]) {
            if (node_id >= node_count) continue;
            for (GraphNodeId input_id : compiled.nodes[node_id].inputs) {
                if (input_id < node_count) {
                    compiled.lifetimes[input_id].last_level = std::max(
                        compiled.lifetimes[input_id].last_level, level_index
                    );
                    compiled.lifetimes[input_id].consumer_count++;
                }
            }
        }
    }

    compiled.release_after_level.assign(compiled.levels.size(), {});
    compiled.ownership_transfers.assign(node_count, CompiledOwnershipTransfer{});
    for (GraphNodeId node_id = 0; node_id < node_count; ++node_id) {
        const auto& lifetime = compiled.lifetimes[node_id];
        // The graph output remains owned by the executor until it returns.
        // Resources with no consumers are likewise not released here.
        if (node_id == compiled.output || lifetime.consumer_count == 0 ||
            lifetime.last_level >= compiled.release_after_level.size()) {
            continue;
        }
        compiled.release_after_level[lifetime.last_level].push_back(node_id);

        if (lifetime.consumer_count == 1 && node_id < compiled.nodes.size() &&
            compiled.nodes[node_id].consumers.size() == 1) {
            compiled.ownership_transfers[node_id] = CompiledOwnershipTransfer{
                node_id, compiled.nodes[node_id].consumers.front(), true};
        }
    }
}
