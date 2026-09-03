#pragma once

#include <chronon3d/runtime/output_sink.hpp>

#include <cstdint>
#include <vector>

namespace chronon3d::runtime {

struct PassTimingReceipt {
    std::uint32_t pass_index{0};
    std::uint32_t node{0};
    std::uint64_t gpu_duration_ns{0};
    bool resolved{false};
};

struct MediaRenderReceipt {
    bool present{false};
    std::uint32_t source_resource{0};
    std::uint32_t destination_resource{0};
    std::uint8_t pixel_format{0};
    std::uint8_t plane_count{0};
    std::uint8_t conversion{0};
    std::uint8_t zero_copy_policy{0};
    std::uint8_t zero_copy_proof{0};
    bool zero_copy_selected{false};
};

/// Immutable completion record for one rendered output. Graph identity, media
/// decision, pass timings and output hash all cross the same finalize boundary.
/// total_gpu_time_ns is derived only from pass_timings; there is no second
/// frame-level timing authority.
struct RenderReceipt {
    std::uint64_t structure_hash{0};
    std::uint64_t graph_instance_id{0};
    std::vector<PassTimingReceipt> pass_timings{};
    std::uint64_t total_gpu_time_ns{0};
    MediaRenderReceipt media{};
    OutputFinalizeResult output{};
};

} // namespace chronon3d::runtime
