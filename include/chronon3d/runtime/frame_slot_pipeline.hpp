#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/timeline/evaluated_composition_frame.hpp>
#include <chronon3d/runtime/bounded_spsc_ring.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace chronon3d::runtime {

enum class FrameSlotState : std::uint8_t {
    Free,
    Evaluating,
    Evaluated,
    Rendered,
    Encoding,
};

struct FrameSlot {
    Frame frame{-1};
    std::uint64_t sequence{0};
    FrameSlotState state{FrameSlotState::Free};
    // Evaluation-owned scene/property allocations live in the slot for the
    // whole evaluate→render interval. Strict capacity makes overflow visible
    // before the hot loop can fall back to malloc.
    FrameArena arena{4u * 1024u * 1024u, true};
    // The slot owns the evaluated scene and rendered output.  Keeping these
    // payloads beside the arena prevents a second, unbounded lifecycle from
    // appearing in the pipeline implementation.
    std::optional<EvaluatedCompositionFrame> evaluated{};
    std::shared_ptr<Framebuffer> rendered{};

    void reset_for_reuse() noexcept {
        evaluated.reset();
        rendered.reset();
        frame = Frame{-1};
        sequence = 0;
        state = FrameSlotState::Free;
        arena.reset();
    }
};

/// Fixed-depth ownership/state machine for a bounded evaluate/render/encode
/// pipeline. The queues carry slot indices, so frame payloads remain owned by
/// the slots and never grow an unbounded work queue.
template <std::size_t Depth>
class FrameSlotPipeline {
    static_assert(Depth > 0, "FrameSlotPipeline depth must be positive");
    using SlotIndexRing = BoundedSpscRing<std::size_t, Depth>;

public:
    FrameSlotPipeline() {
        for (std::size_t i = 0; i < Depth; ++i) {
            // Construction has capacity for every slot, so this cannot fail.
            (void)m_free.try_push(i);
        }
    }

    FrameSlotPipeline(const FrameSlotPipeline&) = delete;
    FrameSlotPipeline& operator=(const FrameSlotPipeline&) = delete;

    [[nodiscard]] static constexpr std::size_t depth() noexcept { return Depth; }

    /// Reserve a free slot for the evaluator. The caller fills frame data,
    /// then calls publish_evaluated().
    [[nodiscard]] FrameSlot* acquire_for_evaluation() noexcept {
        std::size_t index = 0;
        if (!m_free.try_pop(index)) return nullptr;
        m_slots[index].arena.reset();
        m_slots[index].state = FrameSlotState::Evaluating;
        return &m_slots[index];
    }

    [[nodiscard]] bool publish_evaluated(FrameSlot& slot) noexcept {
        if (slot.state != FrameSlotState::Evaluating) return false;
        const auto index = index_of(slot);
        slot.state = FrameSlotState::Evaluated;
        if (!m_evaluated.try_push(index)) {
            slot.state = FrameSlotState::Free;
            (void)m_free.try_push(index);
            return false;
        }
        return true;
    }

    [[nodiscard]] FrameSlot* acquire_for_render() noexcept {
        std::size_t index = 0;
        if (!m_evaluated.try_pop(index)) return nullptr;
        return &m_slots[index];
    }

    [[nodiscard]] bool publish_rendered(FrameSlot& slot) noexcept {
        if (slot.state != FrameSlotState::Evaluated) return false;
        const auto index = index_of(slot);
        slot.state = FrameSlotState::Rendered;
        if (!m_rendered.try_push(index)) {
            slot.state = FrameSlotState::Evaluated;
            (void)m_evaluated.try_push(index);
            return false;
        }
        return true;
    }

    [[nodiscard]] FrameSlot* acquire_for_encoding() noexcept {
        std::size_t index = 0;
        if (!m_rendered.try_pop(index)) return nullptr;
        return &m_slots[index];
    }

    [[nodiscard]] bool begin_encoding(FrameSlot& slot) noexcept {
        if (slot.state != FrameSlotState::Rendered) return false;
        slot.state = FrameSlotState::Encoding;
        return true;
    }

    [[nodiscard]] bool release_encoded(FrameSlot& slot) noexcept {
        if (slot.state != FrameSlotState::Encoding) return false;
        const auto index = index_of(slot);
        slot.reset_for_reuse();
        return m_free.try_push(index);
    }

    /// Return a reserved slot after a stage failure. The caller must pass a
    /// slot that has already been removed from its stage queue.
    [[nodiscard]] bool abort(FrameSlot& slot) noexcept {
        if (slot.state == FrameSlotState::Free) return false;
        const auto index = index_of(slot);
        slot.reset_for_reuse();
        return m_free.try_push(index);
    }

    [[nodiscard]] std::size_t in_flight() const noexcept {
        return Depth - m_free.size();
    }

    [[nodiscard]] std::size_t rendered_depth() const noexcept {
        return m_rendered.size();
    }

    [[nodiscard]] const std::array<FrameSlot, Depth>& slots() const noexcept {
        return m_slots;
    }

    [[nodiscard]] std::array<FrameSlot, Depth>& slots() noexcept {
        return m_slots;
    }

    /// Restore the fixed ring to its construction state.  The caller must
    /// invoke this only after all stage threads have joined.
    void reset() noexcept {
        m_free.reset();
        m_evaluated.reset();
        m_rendered.reset();
        for (std::size_t i = 0; i < Depth; ++i) {
            m_slots[i].reset_for_reuse();
            (void)m_free.try_push(i);
        }
    }

private:
    [[nodiscard]] std::size_t index_of(const FrameSlot& slot) const noexcept {
        return static_cast<std::size_t>(&slot - m_slots.data());
    }

    std::array<FrameSlot, Depth> m_slots{};
    SlotIndexRing m_free;
    SlotIndexRing m_evaluated;
    SlotIndexRing m_rendered;
};

} // namespace chronon3d::runtime
