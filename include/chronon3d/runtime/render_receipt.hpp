#pragma once

#include <chronon3d/runtime/output_sink.hpp>

#include <cstdint>

namespace chronon3d::runtime {

/// Immutable completion record for one rendered output. It is populated from
/// the compiled graph and OutputSink::finalize(), so timing, byte count and
/// output hash all share the same completion boundary.
struct RenderReceipt {
    std::uint64_t structure_hash{0};
    std::uint64_t graph_instance_id{0};
    std::uint64_t gpu_frame_time_ns{0};
    OutputFinalizeResult output{};
};

} // namespace chronon3d::runtime
