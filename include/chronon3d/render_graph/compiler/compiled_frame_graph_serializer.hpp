#pragma once

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/runtime/render_receipt.hpp>

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

namespace chronon3d::graph {

namespace detail {
inline void append_json_string(std::ostringstream& out, std::string_view value) {
    out << '"';
    for (const char ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << ch; break;
        }
    }
    out << '"';
}
} // namespace detail

/// Deterministic diagnostic serializer. Vector order is compilation order and
/// no unordered container participates, so identical compiled graphs produce
/// byte-identical JSON.
[[nodiscard]] inline std::string serialize_compiled_frame_graph(
    const CompiledFrameGraph& graph) {
    std::ostringstream out;
    out << "{\"version\":1"
        << ",\"structure_hash\":" << graph.structure_hash
        << ",\"graph_instance_id\":" << graph.graph_instance_id.value
        << ",\"output\":" << graph.output
        << ",\"resources\":[";

    bool first_resource = true;
    for (const auto& resource : graph.resource_table().resources) {
        if (resource.producer == k_invalid_node) continue;
        if (!first_resource) out << ',';
        first_resource = false;
        out << "{\"producer\":" << resource.producer
            << ",\"width\":" << resource.desc.width
            << ",\"height\":" << resource.desc.height
            << ",\"pixel_format\":"
            << static_cast<unsigned>(resource.desc.format.pixel)
            << ",\"kind\":" << static_cast<unsigned>(resource.desc.kind)
            << ",\"usage\":" << static_cast<unsigned>(resource.desc.usage)
            << ",\"lifetime\":" << static_cast<unsigned>(resource.desc.lifetime)
            << ",\"bytes\":" << resource.physical.allocation_bytes
            << ",\"alignment\":" << resource.physical.alignment
            << ",\"plane_count\":" << resource.physical.plane_count
            << ",\"first_level\":" << resource.first_level
            << ",\"last_level\":" << resource.last_level
            << ",\"consumer_count\":" << resource.consumer_count
            << ",\"release_scheduled\":"
            << (resource.release_scheduled ? "true" : "false")
            << ",\"release_after_level\":" << resource.release_after_level
            << ",\"physical_slot\":" << resource.physical_slot
            << ",\"persistent\":" << (resource.persistent ? "true" : "false")
            << ",\"async_use\":" << (resource.async_use ? "true" : "false")
            << ",\"aliasable\":" << (resource.physical.aliasable ? "true" : "false")
            << ",\"subresources\":[";
        for (std::size_t i = 0; i < resource.subresources.size(); ++i) {
            if (i != 0) out << ',';
            const auto& subresource = resource.subresources[i];
            out << "{\"id\":" << static_cast<unsigned>(subresource.id)
                << ",\"plane\":" << subresource.plane_index << '}';
        }
        out << "],\"transitions\":[";
        for (std::size_t i = 0; i < resource.transitions.size(); ++i) {
            if (i != 0) out << ',';
            const auto& transition = resource.transitions[i];
            out << "{\"consumer\":" << transition.consumer
                << ",\"subresource\":"
                << static_cast<unsigned>(transition.subresource)
                << ",\"ownership_transfer\":"
                << (transition.ownership_transfer ? "true" : "false")
                << '}';
        }
        out << "]}";
    }

    out << "],\"passes\":[";
    for (std::size_t i = 0; i < graph.program.operations.size(); ++i) {
        if (i != 0) out << ',';
        const auto& operation = graph.program.operations[i];
        out << "{\"node\":" << operation.node
            << ",\"physical_slot\":" << operation.output_physical_slot
            << ",\"timing_index\":" << operation.pass_timing;
        if (const auto* timing = graph.program.query_arena.timing(operation.pass_timing)) {
            out << ",\"begin_query\":" << timing->begin_query
                << ",\"end_query\":" << timing->end_query
                << ",\"gpu_duration_ns\":" << timing->gpu_duration_ns
                << ",\"resolved\":" << (timing->resolved ? "true" : "false");
        }
        out << '}';
    }
    out << "]}";
    return out.str();
}

[[nodiscard]] inline runtime::RenderReceipt make_render_receipt(
    const CompiledFrameGraph& graph,
    runtime::OutputSink& sink) {
    runtime::RenderReceipt receipt;
    receipt.structure_hash = graph.structure_hash;
    receipt.graph_instance_id = graph.graph_instance_id.value;
    receipt.gpu_frame_time_ns = graph.program.gpu_frame_time_ns();
    receipt.output = sink.finalize();
    return receipt;
}

[[nodiscard]] inline std::string serialize_render_receipt(
    const runtime::RenderReceipt& receipt) {
    std::ostringstream out;
    out << "{\"version\":1"
        << ",\"structure_hash\":" << receipt.structure_hash
        << ",\"graph_instance_id\":" << receipt.graph_instance_id
        << ",\"gpu_frame_time_ns\":" << receipt.gpu_frame_time_ns
        << ",\"output\":{\"bytes\":" << receipt.output.bytes
        << ",\"hash\":" << receipt.output.hash
        << ",\"mode\":" << static_cast<unsigned>(receipt.output.mode)
        << "}}";
    return out.str();
}

} // namespace chronon3d::graph
