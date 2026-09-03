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
    out << "{\"version\":2"
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

    out << "],\"physical_allocations\":[";
    for (std::size_t i = 0; i < graph.resource_table().slots.size(); ++i) {
        if (i != 0) out << ',';
        const auto& slot = graph.resource_table().slots[i];
        out << "{\"id\":" << i
            << ",\"capacity_bytes\":" << slot.capacity_bytes
            << ",\"offset\":" << slot.offset
            << ",\"last_use\":" << slot.last_use
            << ",\"dedicated\":" << (slot.dedicated ? "true" : "false")
            << '}';
    }

    out << "],\"dependencies\":[";
    bool first_dependency = true;
    for (const auto& node : graph.nodes) {
        for (const auto input : node.inputs) {
            if (!first_dependency) out << ',';
            first_dependency = false;
            out << "{\"from\":" << input << ",\"to\":" << node.id << '}';
        }
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

    out << "],\"media\":";
    if (!graph.render_to_media) {
        out << "null";
    } else {
        const auto& media = *graph.render_to_media;
        out << "{\"source_resource\":" << media.source_resource
            << ",\"destination_resource\":" << media.destination_resource
            << ",\"width\":" << media.destination.width
            << ",\"height\":" << media.destination.height
            << ",\"pixel_format\":"
            << static_cast<unsigned>(media.destination.format.pixel)
            << ",\"primaries\":"
            << static_cast<unsigned>(media.destination.format.primaries)
            << ",\"transfer\":"
            << static_cast<unsigned>(media.destination.format.transfer)
            << ",\"matrix\":"
            << static_cast<unsigned>(media.destination.format.matrix)
            << ",\"range\":"
            << static_cast<unsigned>(media.destination.format.range)
            << ",\"conversion\":" << static_cast<unsigned>(media.conversion)
            << ",\"zero_copy_policy\":"
            << static_cast<unsigned>(media.zero_copy_policy)
            << ",\"zero_copy_proof\":"
            << static_cast<unsigned>(media.zero_copy_proof)
            << ",\"zero_copy_selected\":"
            << (media.zero_copy_selected ? "true" : "false")
            << '}';
    }
    out << '}';
    return out.str();
}

/// Deterministic DOT view of the compiled graph. The authored RenderGraph DOT
/// remains unchanged; this view exposes physical placement and compiled passes.
[[nodiscard]] inline std::string serialize_compiled_frame_graph_dot(
    const CompiledFrameGraph& graph) {
    std::ostringstream out;
    out << "digraph CompiledFrameGraph {\n"
        << "  graph [label=\"compiled structure " << graph.structure_hash << "\"];\n";
    for (const auto& resource : graph.resource_table().resources) {
        if (resource.producer == k_invalid_node) continue;
        out << "  r" << resource.producer << " [shape=box,label=\"resource "
            << resource.producer << "\\nslot " << resource.physical_slot
            << "\\nplanes " << resource.physical.plane_count << "\"];\n";
    }
    for (std::size_t i = 0; i < graph.program.operations.size(); ++i) {
        const auto& operation = graph.program.operations[i];
        out << "  p" << i << " [shape=ellipse,label=\"pass " << i
            << "\\nnode " << operation.node << "\"];\n";
        for (const auto input : operation.inputs) {
            out << "  r" << input << " -> p" << i << ";\n";
        }
        out << "  p" << i << " -> r" << operation.node << ";\n";
    }
    for (const auto& resource : graph.resource_table().resources) {
        if (resource.producer == k_invalid_node) continue;
        for (const auto& transition : resource.transitions) {
            out << "  r" << resource.producer << " -> r" << transition.consumer
                << " [style=dashed,label=\"transition subresource "
                << static_cast<unsigned>(transition.subresource)
                << (transition.ownership_transfer ? " ownership" : "")
                << "\"];\n";
        }
    }
    out << "}\n";
    return out.str();
}

[[nodiscard]] inline runtime::RenderReceipt make_render_receipt(
    const CompiledFrameGraph& graph,
    runtime::OutputSink& sink) {
    runtime::RenderReceipt receipt;
    receipt.structure_hash = graph.structure_hash;
    receipt.graph_instance_id = graph.graph_instance_id.value;
    receipt.pass_timings.reserve(graph.program.operations.size());
    for (std::size_t i = 0; i < graph.program.operations.size(); ++i) {
        const auto& operation = graph.program.operations[i];
        runtime::PassTimingReceipt timing_receipt;
        timing_receipt.pass_index = static_cast<std::uint32_t>(i);
        timing_receipt.node = operation.node;
        if (const auto* timing = graph.program.query_arena.timing(operation.pass_timing)) {
            timing_receipt.gpu_duration_ns = timing->gpu_duration_ns;
            timing_receipt.resolved = timing->resolved;
            if (timing->resolved) receipt.total_gpu_time_ns += timing->gpu_duration_ns;
        }
        receipt.pass_timings.push_back(timing_receipt);
    }
    if (graph.render_to_media) {
        const auto& media = *graph.render_to_media;
        receipt.media.present = true;
        receipt.media.source_resource = media.source_resource;
        receipt.media.destination_resource = media.destination_resource;
        receipt.media.pixel_format =
            static_cast<std::uint8_t>(media.destination.format.pixel);
        if (const auto* destination =
                graph.resource_table().resource_for(media.destination_resource)) {
            receipt.media.plane_count =
                static_cast<std::uint8_t>(destination->physical.plane_count);
        }
        receipt.media.conversion = static_cast<std::uint8_t>(media.conversion);
        receipt.media.zero_copy_policy =
            static_cast<std::uint8_t>(media.zero_copy_policy);
        receipt.media.zero_copy_proof =
            static_cast<std::uint8_t>(media.zero_copy_proof);
        receipt.media.zero_copy_selected = media.zero_copy_selected;
    }
    receipt.output = sink.finalize();
    return receipt;
}

[[nodiscard]] inline std::string serialize_render_receipt(
    const runtime::RenderReceipt& receipt) {
    std::ostringstream out;
    out << "{\"version\":2"
        << ",\"structure_hash\":" << receipt.structure_hash
        << ",\"graph_instance_id\":" << receipt.graph_instance_id
        << ",\"pass_timings\":[";
    for (std::size_t i = 0; i < receipt.pass_timings.size(); ++i) {
        if (i != 0) out << ',';
        const auto& timing = receipt.pass_timings[i];
        out << "{\"pass_index\":" << timing.pass_index
            << ",\"node\":" << timing.node
            << ",\"gpu_duration_ns\":" << timing.gpu_duration_ns
            << ",\"resolved\":" << (timing.resolved ? "true" : "false")
            << '}';
    }
    out << "],\"total_gpu_time_ns\":" << receipt.total_gpu_time_ns
        << ",\"media\":{"
        << "\"present\":" << (receipt.media.present ? "true" : "false")
        << ",\"source_resource\":" << receipt.media.source_resource
        << ",\"destination_resource\":" << receipt.media.destination_resource
        << ",\"pixel_format\":" << static_cast<unsigned>(receipt.media.pixel_format)
        << ",\"plane_count\":" << static_cast<unsigned>(receipt.media.plane_count)
        << ",\"conversion\":" << static_cast<unsigned>(receipt.media.conversion)
        << ",\"zero_copy_policy\":"
        << static_cast<unsigned>(receipt.media.zero_copy_policy)
        << ",\"zero_copy_proof\":"
        << static_cast<unsigned>(receipt.media.zero_copy_proof)
        << ",\"zero_copy_selected\":"
        << (receipt.media.zero_copy_selected ? "true" : "false")
        << "},\"output\":{\"bytes\":" << receipt.output.bytes
        << ",\"hash\":" << receipt.output.hash
        << ",\"mode\":" << static_cast<unsigned>(receipt.output.mode)
        << "}}";
    return out.str();
}

} // namespace chronon3d::graph
