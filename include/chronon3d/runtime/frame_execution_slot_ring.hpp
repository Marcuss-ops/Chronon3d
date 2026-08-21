#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include <chronon3d/runtime/render_surface_handle.hpp>

namespace chronon3d::runtime {

/// Discrete lifecycle states for an execution slot in the streaming pipeline.
enum class FrameSlotState : std::uint8_t {
    Free,
    GpuWriting,
    ReadyForEncode,
    Encoding
};

/// A single execution slot containing surface identity, synchronization tokens,
/// and metadata for non-blocking GPU rendering and asynchronous encoding.
struct FrameExecutionSlot {
    std::size_t slot_id{0};
    RenderSurfaceHandle render_surface{kInvalidRenderSurfaceHandle};
    std::uintptr_t native_surface_ptr{0};  // e.g. AVFrame* or VkImage/CUdeviceptr
    std::uintptr_t gpu_ready_sync{0};       // e.g. CUevent or VkSemaphore
    std::uint64_t frame_index{std::numeric_limits<std::uint64_t>::max()};
    std::atomic<FrameSlotState> state{FrameSlotState::Free};
};

/// Generic bounded slot ring implementing bidirectional producer-consumer
/// synchronization with explicit backpressure and zero dynamic allocations.
class FrameExecutionSlotRing {
public:
    static constexpr std::size_t kDefaultCapacity = 16;

    class SlotLease {
    public:
        SlotLease() noexcept = default;
        SlotLease(const SlotLease&) = delete;
        SlotLease& operator=(const SlotLease&) = delete;
        SlotLease(SlotLease&& other) noexcept
            : m_ring(std::exchange(other.m_ring, nullptr)),
              m_slot(std::exchange(other.m_slot, nullptr)) {}

        SlotLease& operator=(SlotLease&& other) noexcept {
            if (this != &other) {
                release();
                m_ring = std::exchange(other.m_ring, nullptr);
                m_slot = std::exchange(other.m_slot, nullptr);
            }
            return *this;
        }

        ~SlotLease() { release(); }

        [[nodiscard]] bool valid() const noexcept { return m_ring != nullptr && m_slot != nullptr; }
        [[nodiscard]] FrameExecutionSlot& slot() noexcept { return *m_slot; }
        [[nodiscard]] const FrameExecutionSlot& slot() const noexcept { return *m_slot; }

        void mark_ready() noexcept {
            if (m_ring && m_slot) {
                m_ring->mark_ready(m_slot);
            }
        }

        void release() noexcept {
            if (m_ring && m_slot) {
                m_ring->release_slot(m_slot);
                m_ring = nullptr;
                m_slot = nullptr;
            }
        }

    private:
        friend class FrameExecutionSlotRing;
        SlotLease(FrameExecutionSlotRing* ring, FrameExecutionSlot* slot) noexcept
            : m_ring(ring), m_slot(slot) {}

        FrameExecutionSlotRing* m_ring{nullptr};
        FrameExecutionSlot* m_slot{nullptr};
    };

    explicit FrameExecutionSlotRing(std::size_t capacity = kDefaultCapacity)
        : m_slots(capacity) {
        for (std::size_t i = 0; i < m_slots.size(); ++i) {
            m_slots[i].slot_id = i;
            m_slots[i].state.store(FrameSlotState::Free, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return m_slots.size(); }

    /// Acquire a Free slot. Blocks if all slots are currently in flight (backpressure).
    [[nodiscard]] FrameExecutionSlot* acquire_free_slot() {
        std::unique_lock lock(m_mutex);
        m_cv_free.wait(lock, [this]() {
            for (const auto& s : m_slots) {
                if (s.state.load(std::memory_order_acquire) == FrameSlotState::Free) {
                    return true;
                }
            }
            return false;
        });

        for (std::size_t i = 0; i < m_slots.size(); ++i) {
            const std::size_t idx = (m_producer_cursor + i) % m_slots.size();
            if (m_slots[idx].state.load(std::memory_order_acquire) == FrameSlotState::Free) {
                m_producer_cursor = (idx + 1) % m_slots.size();
                m_slots[idx].state.store(FrameSlotState::GpuWriting, std::memory_order_release);
                return &m_slots[idx];
            }
        }
        throw std::runtime_error("FrameExecutionSlotRing: no free slots found after wake");
    }

    /// Acquire via RAII lease.
    [[nodiscard]] SlotLease acquire_lease() {
        return SlotLease(this, acquire_free_slot());
    }

    /// Transition a slot from GpuWriting to ReadyForEncode.
    void mark_ready(FrameExecutionSlot* slot) noexcept {
        if (slot) {
            slot->state.store(FrameSlotState::ReadyForEncode, std::memory_order_release);
        }
    }

    /// Release slot back to Free pool (called by consumer/encoder when finished with surface).
    void release_slot(FrameExecutionSlot* slot) noexcept {
        if (!slot) return;
        slot->state.store(FrameSlotState::Free, std::memory_order_release);
        {
            std::lock_guard lock(m_mutex);
        }
        m_cv_free.notify_one();
    }

    [[nodiscard]] FrameExecutionSlot& slot(std::size_t index) {
        return m_slots.at(index);
    }

    [[nodiscard]] const FrameExecutionSlot& slot(std::size_t index) const {
        return m_slots.at(index);
    }

private:
    std::vector<FrameExecutionSlot> m_slots;
    std::size_t m_producer_cursor{0};
    std::mutex m_mutex;
    std::condition_variable m_cv_free;
};

} // namespace chronon3d::runtime
