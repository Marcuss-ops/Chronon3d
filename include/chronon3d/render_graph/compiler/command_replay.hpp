#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// command_replay.hpp — Fase E: compile-time command replay bridge
//
// The CommandReplayDescriptor bridges the Fase D ParameterRingDescriptor
// to the GPU backend's per-slot command model.  The backend (Vulkan or CUDA
// Graphs) owns N slots (e.g. 3 for triple-buffered); each slot has a
// pre-recorded command program and a pre-allocated parameter buffer at the
// offsets laid out by ParameterRingDescriptor.  Per frame:
//   acquire → write_params(slot, frame) → submit(slot)
//
// This is a compile-time layout struct — the actual VkCommandBuffer or
// cudaGraphExec_t handles are owned by the runtime backend.
//
// Ticket: TICKET-VIDEO-COMPILER-ARCH-V1 §Fase E
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/render_graph/compiler/parameter_ring.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace chronon3d::graph {

/// Per-slot pre-recorded command state (opaque descriptor, resolved at runtime).
struct CommandSlot {
    std::uint32_t slot_index{0};
    std::vector<std::byte> param_buffer;  // size = total_bytes from ring

    [[nodiscard]] bool ready() const noexcept {
        return !param_buffer.empty();
    }
};

/// Compile-time bridge: maps the ParameterRingDescriptor to N slots.
/// The ring's entries tell each operation its offset; the slot's param_buffer
/// is written by ParameterRingWriter.  The runtime backend iterates slots,
/// writing params once per frame into the slot's buffer, then submitting
/// the pre-recorded command program.
struct CommandReplayDescriptor {
    ParameterRingDescriptor ring;
    std::uint32_t          slot_count{3};
    std::uint64_t          total_param_bytes{0};

    /// Allocate one param_buffer per slot (pre-record step).
    [[nodiscard]] std::vector<CommandSlot> allocate_slots() const {
        std::vector<CommandSlot> slots;
        slots.reserve(slot_count);
        for (std::uint32_t i = 0; i < slot_count; ++i) {
            CommandSlot slot;
            slot.slot_index = i;
            slot.param_buffer.resize(ring.total_bytes, std::byte{0});
            slots.push_back(std::move(slot));
        }
        return slots;
    }

    /// Write per-frame parameters into slot `slot_index` using the ring
    /// writer.  `per_op_writer` is called for each ring entry; the caller
    /// writes the parameter payload for that operation.
    template <typename WriteFn>
    bool write_frame(std::span<CommandSlot> slots,
                     std::uint32_t           slot_index,
                     WriteFn&&              per_op_writer) noexcept {
        if (slot_index >= slots.size() || !slots[slot_index].ready()) {
            return false;
        }
        auto& slot = slots[slot_index];
        ParameterRingWriter writer(std::span<std::byte>(slot.param_buffer), ring);
        for (std::size_t i = 0; i < ring.entries.size(); ++i) {
            per_op_writer(i, ring.entries[i], writer);
        }
        return true;
    }

    [[nodiscard]] bool valid() const noexcept { return !ring.empty(); }
};

/// Build the replay descriptor from a compiled template program's ring.
[[nodiscard]] inline CommandReplayDescriptor
build_command_replay(const ParameterRingDescriptor& ring,
                     std::uint32_t slot_count = 3) noexcept {
    CommandReplayDescriptor replay;
    replay.ring       = ring;
    replay.slot_count = slot_count;
    replay.total_param_bytes =
        static_cast<std::uint64_t>(ring.total_bytes) * slot_count;
    return replay;
}

} // namespace chronon3d::graph