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
    Evaluating,
    Evaluated,
    Rendered,
    GpuWriting,
    ReadyForEncode,
    Encoding
};

/// Protocol state for a native surface crossing Vulkan/CUDA/NVENC.  A valid
/// handle is not evidence that the producer has completed its work.
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

/// A single execution slot containing surface identity, synchronization tokens,
/// and metadata for non-blocking GPU rendering and asynchronous encoding.
struct FrameExecutionSlot {
    std::size_t slot_id{0};
    RenderSurfaceHandle render_surface{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle source_surface{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle native_surface{kInvalidRenderSurfaceHandle};
    graph::RenderBackend* backend{nullptr};
    std::atomic<InteropFrameState> interop_state{InteropFrameState::Recyclable};
    std::uintptr_t native_surface_ptr{0};  // e.g. AVFrame* or VkImage/CUdeviceptr
    std::uintptr_t gpu_ready_sync{0};       // e.g. CUevent or VkSemaphore
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
        : m_slots(capacity), m_evaluated(), m_rendered() {
        if (capacity == 0) {
            throw std::invalid_argument("FrameExecutionSlotRing: capacity must be positive");
        }
        m_evaluated.reserve(capacity);
        m_rendered.reserve(capacity);
        for (std::size_t i = 0; i < m_slots.size(); ++i) {
            m_slots[i].slot_id = i;
            m_slots[i].state.store(FrameSlotState::Free, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return m_slots.size(); }

    [[nodiscard]] std::size_t depth() const noexcept { return m_slots.size(); }

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
                m_slots[idx].interop_state.store(InteropFrameState::Recyclable,
                                                 std::memory_order_release);
                m_slots[idx].native_surface_ptr = 0;
                m_slots[idx].gpu_ready_sync = 0;
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

    // Non-blocking evaluate -> render -> encode transitions used by the
    // prepared CPU job.  The same ring therefore owns both video surface
    // lifetime and prepared-frame backpressure; payloads remain external and
    // typed by the caller.
    [[nodiscard]] FrameExecutionSlot* acquire_for_evaluation() noexcept {
        std::lock_guard lock(m_mutex);
        for (std::size_t i = 0; i < m_slots.size(); ++i) {
            const std::size_t index = (m_producer_cursor + i) % m_slots.size();
            auto& candidate = m_slots[index];
            if (candidate.state.load(std::memory_order_acquire) == FrameSlotState::Free) {
                m_producer_cursor = (index + 1) % m_slots.size();
                candidate.state.store(FrameSlotState::Evaluating, std::memory_order_release);
                return &candidate;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool publish_evaluated(FrameExecutionSlot& slot) noexcept {
        std::lock_guard lock(m_mutex);
        if (slot.state.load(std::memory_order_acquire) != FrameSlotState::Evaluating) return false;
        slot.state.store(FrameSlotState::Evaluated, std::memory_order_release);
        enqueue(m_evaluated, m_evaluated_head, &slot);
        m_cv_free.notify_all();
        return true;
    }

    [[nodiscard]] FrameExecutionSlot* acquire_for_render() noexcept {
        std::lock_guard lock(m_mutex);
        if (m_evaluated.empty()) return nullptr;
        auto* slot = dequeue(m_evaluated, m_evaluated_head);
        return slot;
    }

    [[nodiscard]] bool publish_rendered(FrameExecutionSlot& slot) noexcept {
        std::lock_guard lock(m_mutex);
        if (slot.state.load(std::memory_order_acquire) != FrameSlotState::Evaluated) return false;
        slot.state.store(FrameSlotState::Rendered, std::memory_order_release);
        enqueue(m_rendered, m_rendered_head, &slot);
        m_cv_free.notify_all();
        return true;
    }

    [[nodiscard]] FrameExecutionSlot* acquire_for_encoding() noexcept {
        std::lock_guard lock(m_mutex);
        if (m_rendered.empty()) return nullptr;
        auto* slot = dequeue(m_rendered, m_rendered_head);
        return slot;
    }

    [[nodiscard]] bool begin_encoding(FrameExecutionSlot& slot) noexcept {
        if (slot.state.load(std::memory_order_acquire) != FrameSlotState::Rendered) return false;
        slot.state.store(FrameSlotState::Encoding, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool release_encoded(FrameExecutionSlot& slot) noexcept {
        if (slot.state.load(std::memory_order_acquire) != FrameSlotState::Encoding) return false;
        release_slot(&slot);
        return true;
    }

    [[nodiscard]] bool abort(FrameExecutionSlot& slot) noexcept {
        if (slot.state.load(std::memory_order_acquire) == FrameSlotState::Free) return false;
        release_slot(&slot);
        return true;
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
        slot->interop_state.store(InteropFrameState::Recyclable,
                                  std::memory_order_release);
        slot->native_surface_ptr = 0;
        slot->gpu_ready_sync = 0;
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

    [[nodiscard]] std::size_t in_flight() const noexcept { return busy_count(); }

    [[nodiscard]] std::size_t rendered_depth() const noexcept {
        std::lock_guard lock(m_mutex);
        return m_rendered.size() - m_rendered_head;
    }

    void reset() noexcept {
        std::lock_guard lock(m_mutex);
        m_evaluated.clear();
        m_evaluated_head = 0;
        m_rendered.clear();
        m_rendered_head = 0;
        m_completions.clear();
        for (auto& slot : m_slots) {
            slot.state.store(FrameSlotState::Free, std::memory_order_release);
            slot.interop_state.store(InteropFrameState::Recyclable,
                                     std::memory_order_release);
            slot.frame_index = std::numeric_limits<std::uint64_t>::max();
        }
        m_producer_cursor = 0;
        m_cv_free.notify_all();
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
                it->first->interop_state.store(InteropFrameState::Recyclable,
                                               std::memory_order_release);
                it->first->native_surface_ptr = 0;
                it->first->gpu_ready_sync = 0;
                it->first->state.store(FrameSlotState::Free, std::memory_order_release);
                it = m_completions.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::vector<FrameExecutionSlot> m_slots;
    std::size_t m_producer_cursor{0};
    mutable std::mutex m_mutex;
    std::condition_variable m_cv_free;
    mutable std::unordered_map<FrameExecutionSlot*, std::shared_ptr<GpuCompletion>> m_completions;
    std::vector<FrameExecutionSlot*> m_evaluated;
    std::vector<FrameExecutionSlot*> m_rendered;
    std::size_t m_evaluated_head{0};
    std::size_t m_rendered_head{0};

    static void enqueue(std::vector<FrameExecutionSlot*>& queue,
                        std::size_t& head,
                        FrameExecutionSlot* slot) noexcept {
        if (head != 0 && queue.size() == queue.capacity()) {
            const auto live = queue.size() - head;
            std::move(queue.begin() + static_cast<std::ptrdiff_t>(head),
                      queue.end(), queue.begin());
            queue.resize(live);
            head = 0;
        }
        queue.push_back(slot);
    }

    static FrameExecutionSlot* dequeue(std::vector<FrameExecutionSlot*>& queue,
                                       std::size_t& head) noexcept {
        auto* slot = queue[head++];
        if (head == queue.size()) {
            queue.clear();
            head = 0;
        }
        return slot;
    }
    mutable std::atomic<std::uint64_t> m_wait_count{0};
    mutable std::atomic<std::uint64_t> m_wait_us{0};
    bool m_closed{false};
};

} // namespace chronon3d::runtime
