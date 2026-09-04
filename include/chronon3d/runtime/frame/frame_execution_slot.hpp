#pragma once

#include <chronon3d/runtime/render_surface_handle.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace chronon3d::graph { class RenderBackend; }

namespace chronon3d::runtime {

using FrameSlotId = std::size_t;
inline constexpr std::size_t kMaxFrameExecutionSlots = 256;

class GpuCompletion {
public:
    virtual ~GpuCompletion() = default;
    [[nodiscard]] virtual bool ready() const noexcept = 0;
    virtual void wait() = 0;
};

/// Pipeline ownership state. This is the only lifecycle state physically
/// stored on a frame slot.
enum class FrameSlotState : std::uint8_t {
    Free,
    Evaluating,
    Evaluated,
    Rendered,
    GpuWriting,
    ReadyForEncode,
    Encoding
};

/// Protocol state for a native surface crossing Vulkan/CUDA/NVENC. Storage for
/// this state belongs to GpuCompletionTracker, not FrameExecutionSlot.
enum class InteropFrameState : std::uint8_t {
    Allocated,
    VulkanRecording,
    VulkanSubmitted,
    VulkanComplete,
    CudaAcquired,
    CudaReady,
    EncodeSubmitted,
    EncodeConsumed,
    Recyclable,
};

[[nodiscard]] constexpr bool valid_interop_transition(
    InteropFrameState from, InteropFrameState to) noexcept {
    if (to == InteropFrameState::Recyclable) return true;
    switch (from) {
        case InteropFrameState::Recyclable:
            return to == InteropFrameState::Allocated;
        case InteropFrameState::Allocated:
            return to == InteropFrameState::VulkanRecording;
        case InteropFrameState::VulkanRecording:
            return to == InteropFrameState::VulkanSubmitted;
        case InteropFrameState::VulkanSubmitted:
            return to == InteropFrameState::VulkanComplete;
        case InteropFrameState::VulkanComplete:
            return to == InteropFrameState::CudaAcquired ||
                   to == InteropFrameState::EncodeSubmitted;
        case InteropFrameState::CudaAcquired:
            return to == InteropFrameState::CudaReady;
        case InteropFrameState::CudaReady:
            return to == InteropFrameState::EncodeSubmitted;
        case InteropFrameState::EncodeSubmitted:
            return to == InteropFrameState::EncodeConsumed;
        case InteropFrameState::EncodeConsumed:
            return false;
    }
    return false;
}

/// Tracker-owned atomic storage. FrameExecutionSlot only receives a bound
/// non-owning reference to one cell, eliminating the second state machine from
/// slot storage while preserving the existing transition API at call sites.
struct InteropFrameStateCell {
    std::atomic<InteropFrameState> value{InteropFrameState::Recyclable};
};

class InteropFrameStateRef {
public:
    InteropFrameStateRef() noexcept = default;

    void bind(InteropFrameStateCell* cell) noexcept { cell_ = cell; }

    [[nodiscard]] InteropFrameState load(
        std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return cell_ ? cell_->value.load(order) : InteropFrameState::Recyclable;
    }

    void store(InteropFrameState value,
               std::memory_order order = std::memory_order_seq_cst) noexcept {
        if (cell_) cell_->value.store(value, order);
    }

    bool compare_exchange_weak(
        InteropFrameState& expected,
        InteropFrameState desired,
        std::memory_order success,
        std::memory_order failure) noexcept {
        if (!cell_) return false;
        return cell_->value.compare_exchange_weak(expected, desired, success, failure);
    }

    [[nodiscard]] bool bound() const noexcept { return cell_ != nullptr; }

private:
    InteropFrameStateCell* cell_{nullptr};
};

/// Fixed-pool slot payload. Surface identity remains on the slot; GPU protocol
/// state and completion ownership live in GpuCompletionTracker.
struct FrameExecutionSlot {
    FrameSlotId slot_id{0};
    RenderSurfaceHandle render_surface{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle source_surface{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle native_surface{kInvalidRenderSurfaceHandle};
    graph::RenderBackend* backend{nullptr};
    InteropFrameStateRef interop_state{};
    std::uintptr_t native_surface_ptr{0};
    std::uintptr_t gpu_ready_sync{0};
    std::uint64_t frame_index{std::numeric_limits<std::uint64_t>::max()};
    std::atomic<FrameSlotState> state{FrameSlotState::Free};

    [[nodiscard]] bool native_surface_prepared() const noexcept {
        const auto current = interop_state.load(std::memory_order_acquire);
        return current == InteropFrameState::VulkanComplete ||
               current == InteropFrameState::CudaReady;
    }

    [[nodiscard]] bool transition_interop_state(InteropFrameState next) noexcept {
        auto current = interop_state.load(std::memory_order_acquire);
        while (valid_interop_transition(current, next)) {
            if (interop_state.compare_exchange_weak(
                    current, next, std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return true;
            }
        }
        return false;
    }
};

} // namespace chronon3d::runtime
