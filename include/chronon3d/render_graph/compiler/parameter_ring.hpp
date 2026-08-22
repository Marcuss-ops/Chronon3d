#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// parameter_ring.hpp — Fase D: compile-time parameter ring descriptor
//
// The ParameterRing maps every CompiledOperation that carries a parameter
// block (parameter_size > 0) to a fixed offset in a pre-allocated buffer.
// At runtime, the ring is indexed by frame slot; the FrameParameterWriter
// copies the parameter block into the correct slot without allocation,
// re-sizing, or per-frame offset discovery.
//
// This is the compile→runtime bridge: the compiler builds the ring once;
// the hot loop does `ring.write_params(slot_index, frame, params)`.
//
// Ticket: TICKET-VIDEO-COMPILER-ARCH-V1 §Fase D
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace chronon3d::graph {

// ── ParameterRingDescriptor ──────────────────────────────────────────────────
//
/// Immutable-after-compile layout of the parameter ring.  One entry per
/// operation with parameters; the runtime buffer is `total_bytes` × N slots.
struct ParameterRingDescriptor {
    std::uint32_t slot_count{0};   // number of frame slots
    std::uint32_t total_bytes{0};  // bytes per slot (sum of all parameter_sizes)

    /// Per-entry mapping: each entry corresponds to one CompiledOperation
    /// that has parameter_size > 0.  `offset` is relative to the slot base.
    struct Entry {
        GraphNodeId   node{k_invalid_node};
        StableNodeId  stable_node{kInvalidStableNodeId};
        std::uint32_t offset{0};
        std::uint32_t size{0};
    };
    std::vector<Entry> entries;

    [[nodiscard]] bool empty() const noexcept {
        return entries.empty() || total_bytes == 0;
    }

    [[nodiscard]] std::size_t buffer_bytes() const noexcept {
        return static_cast<std::size_t>(slot_count) * total_bytes;
    }

    [[nodiscard]] const Entry* entry_for(GraphNodeId node) const noexcept {
        for (const auto& e : entries) {
            if (e.node == node) return &e;
        }
        return nullptr;
    }
};

// ── ParameterRingWriter (runtime) ──────────────────────────────────────────
//
/// Copies per-frame parameter data into a pre-allocated slot buffer.
/// The slot buffer is `total_bytes` and is indexed by the descriptor entries.
class ParameterRingWriter {
public:
    /// Construct a writer that targets `slot_buffer` (size = descriptor.total_bytes).
    ParameterRingWriter(std::span<std::byte> slot_buffer,
                         const ParameterRingDescriptor& descriptor) noexcept
        : m_buffer(slot_buffer)
        , m_descriptor(&descriptor) {}

    /// Write raw parameter bytes for the node at `entry_index` in the
    /// descriptor's entries list.  Returns false when index or size overflow.
    bool write_entry(std::size_t entry_index,
                     std::span<const std::byte> param_bytes) noexcept {
        if (entry_index >= m_descriptor->entries.size()) {
            return false;
        }
        const auto& entry = m_descriptor->entries[entry_index];
        if (entry.offset + param_bytes.size() > m_buffer.size()) {
            return false;
        }
        auto* dst = m_buffer.data() + entry.offset;
        std::memcpy(dst, param_bytes.data(),
                    std::min(param_bytes.size(),
                             static_cast<std::size_t>(entry.size)));
        return true;
    }

    /// Clear the entire slot buffer to zero (pre-frame reset).
    void clear() noexcept {
        std::memset(m_buffer.data(), 0, m_buffer.size());
    }

    [[nodiscard]] std::span<std::byte> buffer() const noexcept {
        return m_buffer;
    }

private:
    std::span<std::byte>         m_buffer;
    const ParameterRingDescriptor* m_descriptor;
};

// ── Compile-time builder ─────────────────────────────────────────────────────
//
/// Build a ParameterRingDescriptor from the compiled frame graph's operations.
/// `slot_count` is the ring size (e.g. 3 for a triple-buffered GPU pipeline).
/// Each operation with `parameter_size > 0` gets an entry; offsets are
/// assigned sequentially.
[[nodiscard]] inline ParameterRingDescriptor
build_parameter_ring(const CompiledFrameGraph& compiled,
                     std::uint32_t slot_count) {
    ParameterRingDescriptor ring;
    ring.slot_count = slot_count;

    std::uint32_t offset{0};
    for (const auto& op : compiled.program.operations) {
        if (op.parameter_size == 0) continue;
        if (op.node >= compiled.nodes.size()) continue;

        ParameterRingDescriptor::Entry entry;
        entry.node        = op.node;
        entry.stable_node = op.stable_node;
        entry.offset      = offset;
        entry.size        = op.parameter_size;

        ring.entries.push_back(entry);
        offset += op.parameter_size;
    }

    ring.total_bytes = offset;
    return ring;
}

} // namespace chronon3d::graph