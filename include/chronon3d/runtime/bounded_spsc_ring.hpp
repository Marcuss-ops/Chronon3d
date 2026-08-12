#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

namespace chronon3d::runtime {

/// Fixed-capacity single-producer/single-consumer ring.
///
/// The ring owns no threads and performs no allocation after construction.
/// Producer and consumer must each be confined to one thread.  The monotonic
/// counters make the full/empty conditions explicit and keep queue memory
/// bounded even when a downstream stage is slower than its producer.
template <typename T, std::size_t Capacity>
class BoundedSpscRing {
    static_assert(Capacity > 0, "BoundedSpscRing capacity must be positive");

public:
    BoundedSpscRing() = default;
    BoundedSpscRing(const BoundedSpscRing&) = delete;
    BoundedSpscRing& operator=(const BoundedSpscRing&) = delete;

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

    [[nodiscard]] bool try_push(T value) {
        const auto head = m_head.load(std::memory_order_relaxed);
        const auto tail = m_tail.load(std::memory_order_acquire);
        if (head - tail == Capacity) return false;

        m_storage[head % Capacity].emplace(std::move(value));
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(T& value) {
        const auto tail = m_tail.load(std::memory_order_relaxed);
        const auto head = m_head.load(std::memory_order_acquire);
        if (tail == head) return false;

        auto& entry = m_storage[tail % Capacity];
        value = std::move(*entry);
        entry.reset();
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        const auto head = m_head.load(std::memory_order_acquire);
        const auto tail = m_tail.load(std::memory_order_acquire);
        return head - tail;
    }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] bool full() const noexcept { return size() == Capacity; }

    // Called only after producer and consumer have stopped.  This is used by
    // a pipeline owner when a stage fails and all in-flight slots must be
    // returned before the job can be reused.
    void reset() noexcept {
        for (auto& entry : m_storage) entry.reset();
        m_head.store(0, std::memory_order_relaxed);
        m_tail.store(0, std::memory_order_relaxed);
    }

private:
    std::array<std::optional<T>, Capacity> m_storage{};
    alignas(64) std::atomic<std::size_t> m_head{0};
    alignas(64) std::atomic<std::size_t> m_tail{0};
};

} // namespace chronon3d::runtime
