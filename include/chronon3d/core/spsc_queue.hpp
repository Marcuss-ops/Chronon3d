#pragma once

#include <atomic>
#include <vector>
#include <optional>
#include <cassert>

namespace chronon3d {

/**
 * @brief A lock-free Single-Producer Single-Consumer (SPSC) queue.
 * 
 * This is a circular buffer implementation that avoids locks and condition variables
 * for the hot path. It requires the producer and consumer to be on different threads.
 * 
 * Note: Capacity must be a power of two for performance.
 */
template <typename T>
class SPSCQueue {
public:
    explicit SPSCQueue(size_t capacity)
        : m_capacity(capacity)
        , m_mask(capacity - 1)
        , m_buffer(capacity)
    {
        // Capacity must be power of two
        assert((capacity & (capacity - 1)) == 0 && "Capacity must be a power of two");
    }

    /**
     * @brief Pushes an item into the queue.
     * @return true if successful, false if the queue is full.
     */
    bool try_push(T&& item) {
        const size_t head = m_head.load(std::memory_order_relaxed);
        if (head - m_tail_cache >= m_capacity) {
            m_tail_cache = m_tail.load(std::memory_order_acquire);
            if (head - m_tail_cache >= m_capacity) {
                return false;
            }
        }

        m_buffer[head & m_mask] = std::move(item);
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pops an item from the queue.
     * @return The item if successful, std::nullopt if the queue is empty.
     */
    std::optional<T> try_pop() {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head_cache) {
            m_head_cache = m_head.load(std::memory_order_acquire);
            if (tail == m_head_cache) {
                return std::nullopt;
            }
        }

        T item = std::move(m_buffer[tail & m_mask]);
        m_tail.store(tail + 1, std::memory_order_release);
        return item;
    }

    [[nodiscard]] size_t capacity() const { return m_capacity; }
    
    [[nodiscard]] size_t size() const {
        const size_t head = m_head.load(std::memory_order_acquire);
        const size_t tail = m_tail.load(std::memory_order_acquire);
        return head - tail;
    }

    [[nodiscard]] bool empty() const {
        return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire);
    }

private:
    const size_t m_capacity;
    const size_t m_mask;
    std::vector<T> m_buffer;

    // Cache head/tail to reduce atomic contention
    size_t m_head_cache{0};
    size_t m_tail_cache{0};

    alignas(64) std::atomic<size_t> m_head{0};
    alignas(64) std::atomic<size_t> m_tail{0};
};

} // namespace chronon3d
