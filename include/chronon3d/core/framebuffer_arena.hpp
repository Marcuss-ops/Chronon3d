#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/memory_utils.hpp>
#include <mutex>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <limits>

namespace chronon3d {

/**
 * @brief A large pre-allocated memory block for Framebuffers.
 *
 * Designed to eliminate allocation overhead (mmap/munmap) during the render loop.
 * Uses a simple linear allocator (arena) pattern backed by a MemoryBlock.
 */
class FramebufferArena {
public:
    explicit FramebufferArena(size_t total_bytes)
        : m_total_bytes(total_bytes)
    {
        if (total_bytes == 0) {
            throw std::invalid_argument("FramebufferArena: total_bytes must be > 0");
        }

        m_block = memory::allocate_memory_block(total_bytes);
        if (!m_block.data) {
            throw std::bad_alloc();
        }

        m_current_ptr = static_cast<uint8_t*>(m_block.data);
    }

    ~FramebufferArena() = default;  // MemoryBlock RAII handles cleanup

    // No copy / no move (pointers into the block would be invalidated)
    FramebufferArena(const FramebufferArena&) = delete;
    FramebufferArena& operator=(const FramebufferArena&) = delete;
    FramebufferArena(FramebufferArena&&) = delete;
    FramebufferArena& operator=(FramebufferArena&&) = delete;

    /**
     * @brief Allocates a block of memory from the arena.
     * @return Pointer to the allocated memory, or nullptr if out of space.
     */
    void* allocate(size_t bytes) {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Align to 64 bytes for SIMD friendliness.
        // Guard against overflow: if bytes is huge, bytes+63 could wrap.
        if (bytes > std::numeric_limits<size_t>::max() - 63) {
            return nullptr;
        }
        const size_t aligned_bytes = (bytes + 63) & ~size_t(63);

        // Overflow check on offset addition
        if (m_offset + aligned_bytes > m_total_bytes ||
            m_offset + aligned_bytes < m_offset) {
            return nullptr;
        }

        void* ptr = m_current_ptr;
        m_current_ptr += aligned_bytes;
        m_offset += aligned_bytes;
        return ptr;
    }

    /**
     * @brief Resets the arena to the beginning.
     * DANGEROUS: Ensure no framebuffers from the arena are still in use!
     */
    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_current_ptr = static_cast<uint8_t*>(m_block.data);
        m_offset = 0;
    }

    [[nodiscard]] size_t total_bytes() const { return m_total_bytes; }

    [[nodiscard]] size_t used_bytes() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_offset;
    }

    [[nodiscard]] double usage_ratio() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_total_bytes == 0) return 0.0;
        return static_cast<double>(m_offset) / m_total_bytes;
    }

private:
    memory::MemoryBlock m_block;
    uint8_t* m_current_ptr{nullptr};
    size_t m_total_bytes{0};
    size_t m_offset{0};
    mutable std::mutex m_mutex;
};

} // namespace chronon3d
