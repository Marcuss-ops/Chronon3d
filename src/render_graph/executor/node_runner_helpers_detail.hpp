#include <fmt/format.h>

namespace {

// OPT-TRACKER-LOOKUP — the per-node memory tracker id used to be built as
// "g<n>:n<m>" with two std::to_string calls + concatenation (2-3 heap
// allocations per node per frame, on the render hot path). The tracker only
// needs a string_view for its map lookup, so the id is now formatted into a
// caller-provided stack buffer: zero heap allocations. The format is
// byte-identical to the previous heap-built string, so report keys and
// telemetry node ids are unchanged.
constexpr std::size_t kMemoryNodeIdCapacity =
    1 /*g*/ + 20 /*u64 max*/ + 2 /*:n*/ + 20 /*u64 max*/ + 1 /*NUL*/;

[[nodiscard]] std::string_view format_memory_node_id(
    char (&buffer)[kMemoryNodeIdCapacity],
    const CompiledFrameGraph& compiled,
    GraphNodeId id,
    std::string_view fallback) {
    if (id < compiled.nodes.size() && compiled.nodes[id].reachable &&
        compiled.graph_instance_id != kInvalidGraphInstanceId &&
        compiled.nodes[id].stable_node_id != kInvalidStableNodeId) {
        // fmt::format_to with a fixed buffer; int_range check keeps the
        // write bounded by construction.
        const auto result = fmt::format_to_n(
            buffer, kMemoryNodeIdCapacity - 1, "g{}:n{}",
            compiled.graph_instance_id.value,
            compiled.nodes[id].stable_node_id.value);
        return std::string_view{buffer, result.size};
    }
    return fallback;
}

[[nodiscard]] bool diag_exec_logging_enabled() noexcept {
    // OPT-DIAG-DEFAULT — the per-node×frame [DIAG-exec] log line is a debug
    // channel, not a production contract: one formatted spdlog::info per
    // executed node per frame flushes I/O on every work unit of the render
    // loop. It is OPT-IN: unset/disabled env keeps the executor silent; an
    // explicit CHRONON3D_DIAG_EXEC_LOG=1|true|on re-enables the line.
    static const bool enabled = [] {
        const char* value = std::getenv("CHRONON3D_DIAG_EXEC_LOG");
        if (!value) return false;
        const std::string_view setting(value);
        return setting == "1" || setting == "true" || setting == "on";
    }();
    return enabled;
}

} // namespace
