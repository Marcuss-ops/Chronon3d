namespace {

[[nodiscard]] bool diag_exec_logging_enabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("CHRONON3D_DIAG_EXEC_LOG");
        if (!value) return true;
        const std::string_view setting(value);
        return setting != "0" && setting != "false" && setting != "off";
    }();
    return enabled;
}

[[nodiscard]] std::string memory_node_id(
    const CompiledFrameGraph& compiled,
    GraphNodeId id,
    std::string_view fallback) {
    if (id < compiled.nodes.size() && compiled.nodes[id].reachable &&
        compiled.graph_instance_id != kInvalidGraphInstanceId &&
        compiled.nodes[id].stable_node_id != kInvalidStableNodeId) {
        return "g" + std::to_string(compiled.graph_instance_id.value) +
               ":n" + std::to_string(compiled.nodes[id].stable_node_id.value);
    }
    return std::string(fallback);
}

} // namespace
