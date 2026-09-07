#include <charconv>
#include <string>
#include <string_view>

namespace {

// HOT-PATH TAX (TICKET-HOTPATH-ZERO-ALLOC-P1) — opt-in diagnostic gate.
//
// This gate used to default to TRUE (env unset ⇒ enabled), which emitted one
// spdlog::info line with 8 formatted args per node × frame from
// execute_single_node. That violated the P1.0 contract already documented in
// source_node_execute.cpp ("Formatted per-frame logging is forbidden in the
// hot loop") and diverged from the two sibling gates
// (CHRONON3D_NATIVE_SURFACE_PROMOTION_DIAG, CHRONON3D_SURFACE_LIFECYCLE_DIAG)
// which are both opt-in. The default is now OFF; set the env var to a
// non-empty, non-falsy value ("1", "true", "on") to re-enable the [DIAG-exec]
// log stream.
[[nodiscard]] bool diag_exec_logging_enabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("CHRONON3D_DIAG_EXEC_LOG");
        if (!value) return false;
        const std::string_view setting(value);
        if (setting.empty()) return false;
        return setting != "0" && setting != "false" && setting != "off";
    }();
    return enabled;
}

// HOT-PATH TAX (TICKET-HOTPATH-ZERO-ALLOC-P1) — allocation-free node id.
//
// memory_node_id() runs once per node × frame whenever a session memory
// tracker is attached (which is always: RenderSession owns one eagerly).
// The previous implementation paid two std::to_string calls plus three
// operator+ concatenations per call: SSO covered the typical short ids, but
// large (graph_instance, stable_node) values spilled to the heap several
// times per call.
// The id is now composed via std::to_chars into a 64-byte STACK buffer —
// worst case "g" + 20 digits + ":n" + 20 digits = 43 chars — and the
// returned std::string is constructed from the exact written range, so it
// lands in the SSO buffer for every id that fits (libstdc++ ≤15 chars,
// libc++ ≤22) and costs at most ONE allocation for pathological ids (the
// original paid up to four). The emitted format is byte-identical to the
// previous "g" + std::to_string(graph) + ":n" + std::to_string(node), so
// NodeMemoryTracker report keys and the telemetry record joins are unchanged.
[[nodiscard]] std::string memory_node_id(
    const CompiledFrameGraph& compiled,
    GraphNodeId id,
    std::string_view fallback) {
    if (id < compiled.nodes.size() && compiled.nodes[id].reachable &&
        compiled.graph_instance_id != kInvalidGraphInstanceId &&
        compiled.nodes[id].stable_node_id != kInvalidStableNodeId) {
        char buffer[64];
        char* begin = buffer;
        char* end = buffer + sizeof(buffer);
        begin[0] = 'g';
        auto g = std::to_chars(begin + 1, end,
            static_cast<unsigned long long>(compiled.graph_instance_id.value));
        // Reserve ":n" (2 chars) between the two numbers.
        auto n = std::to_chars(g.ptr + 2, end,
            static_cast<unsigned long long>(compiled.nodes[id].stable_node_id.value));
        if (n.ec == std::errc{}) {
            g.ptr[0] = ':';
            g.ptr[1] = 'n';
            return std::string(begin, n.ptr);
        }
        // Unreachable for two u64 values in a 64-byte buffer; fall through
        // to the fallback path below rather than emitting a truncated id.
    }
    return std::string(fallback);
}

} // namespace
