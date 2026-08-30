#pragma once

#include <array>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>
#include <unordered_map>

#include <chronon3d/runtime/render_surface_handle.hpp>
#include <chronon3d/core/cancellation_token.hpp>

namespace chronon3d::graph { class RenderBackend; }

namespace chronon3d::runtime {

class GpuCompletion {
public:
    virtual ~GpuCompletion() = default;
    [[nodiscard]] virtual bool ready() const noexcept = 0;
    virtual void wait() = 0;
};

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
    RenderSurfaceHandle source_surface{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle native_surface{kInvalidRenderSurfaceHandle};
    graph::RenderBackend* backend{nullptr};
    bool native_surface_ready{false};
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

        void retire(std::shared_ptr<GpuCompletion> completion) noexcept {
            if (m_ring && m_slot) {
                m_ring->retire_slot(m_slot, std::move(completion));
                m_ring = nullptr;
                m_slot = nullptr;
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
    [[nodiscard]] FrameExecutionSlot* acquire_free_slot(
        const CancellationToken* token = nullptr) {
        std::unique_lock lock(m_mutex);
        const auto wait_start = std::chrono::steady_clock::now();
        for (;;) {
            reap_ready_completions_locked();
            if (m_closed || (token && token->is_cancelled())) return nullptr;
            bool free_slot = false;
            for (const auto& s : m_slots) {
                if (s.state.load(std::memory_order_acquire) == FrameSlotState::Free) {
                    free_slot = true;
                    break;
                }
            }
            if (free_slot) break;
            m_cv_free.wait_for(lock, std::chrono::milliseconds(1));
        }
        const auto waited_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - wait_start).count());
        if (waited_us != 0) {
            m_wait_count.fetch_add(1, std::memory_order_relaxed);
            m_wait_us.fetch_add(waited_us, std::memory_order_relaxed);
        }
        for (std::size_t i = 0; i < m_slots.size(); ++i) {
            const std::size_t idx = (m_producer_cursor + i) % m_slots.size();
            auto completion = m_completions.find(&m_slots[idx]);
            if (completion != m_completions.end() && completion->second->ready()) {
                m_completions.erase(completion);
                m_slots[idx].state.store(FrameSlotState::Free, std::memory_order_release);
            }
            if (m_slots[idx].state.load(std::memory_order_acquire) == FrameSlotState::Free) {
                m_producer_cursor = (idx + 1) % m_slots.size();
                m_slots[idx].state.store(FrameSlotState::GpuWriting, std::memory_order_release);
                return &m_slots[idx];
            }
        }
        throw std::runtime_error("FrameExecutionSlotRing: no free slots found after wake");
    }

    /// Acquire via RAII lease.
    [[nodiscard]] SlotLease acquire_lease(const CancellationToken* token = nullptr) {
        return SlotLease(this, acquire_free_slot(token));
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
            m_completions.erase(slot);
        }
        m_cv_free.notify_one();
    }

    void close() noexcept {
        {
            std::lock_guard lock(m_mutex);
            m_closed = true;
        }
        m_cv_free.notify_all();
    }

    [[nodiscard]] std::size_t busy_count() const noexcept {
        std::size_t count = 0;
        for (const auto& slot : m_slots) {
            if (slot.state.load(std::memory_order_acquire) != FrameSlotState::Free) ++count;
        }
        return count;
    }

    [[nodiscard]] std::uint64_t wait_count() const noexcept {
        return m_wait_count.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t wait_us() const noexcept {
        return m_wait_us.load(std::memory_order_relaxed);
    }

    void retire_slot(FrameExecutionSlot* slot,
                     std::shared_ptr<GpuCompletion> completion) noexcept {
        if (!slot) return;
        {
            std::lock_guard lock(m_mutex);
            if (completion) m_completions[slot] = std::move(completion);
        }
        slot->state.store(FrameSlotState::Encoding, std::memory_order_release);
        m_cv_free.notify_all();
    }

    [[nodiscard]] FrameExecutionSlot& slot(std::size_t index) {
        return m_slots.at(index);
    }

    [[nodiscard]] const FrameExecutionSlot& slot(std::size_t index) const {
        return m_slots.at(index);
    }

private:
    void reap_ready_completions_locked() const noexcept {
        for (auto it = m_completions.begin(); it != m_completions.end();) {
            if (it->second && it->second->ready()) {
                it->first->state.store(FrameSlotState::Free, std::memory_order_release);
                it = m_completions.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::vector<FrameExecutionSlot> m_slots;
    std::size_t m_producer_cursor{0};
    std::mutex m_mutex;
    std::condition_variable m_cv_free;
    mutable std::unordered_map<FrameExecutionSlot*, std::shared_ptr<GpuCompletion>> m_completions;
    mutable std::atomic<std::uint64_t> m_wait_count{0};
    mutable std::atomic<std::uint64_t> m_wait_us{0};
    bool m_closed{false};
};

} // namespace chronon3d::runtime
