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
