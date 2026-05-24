#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/memory_utils.hpp>
#include <mutex>
#include <vector>
#include <cstdint>

namespace chronon3d {

/**
 * @brief A large pre-allocated memory block for Framebuffers.
 * 
 * Designed to eliminate allocation overhead (mmap/munmap) during the render loop.
 * It uses a simple linear allocator (arena) pattern.
 */
class FramebufferArena {
public:
    explicit FramebufferArena(size_t total_bytes)
        : m_total_bytes(total_bytes)
    {
        m_base_ptr = memory::allocate_huge_pages(total_bytes);
        m_current_ptr = static_cast<uint8_t*>(m_base_ptr);
    }

    ~FramebufferArena() {
        if (m_base_ptr) {
            memory::free_huge_pages(m_base_ptr, m_total_bytes);
        }
    }

    // No copy
    FramebufferArena(const FramebufferArena&) = delete;
    FramebufferArena& operator=(const FramebufferArena&) = delete;

    /**
     * @brief Allocates a block of memory from the arena.
     * @return Pointer to the allocated memory, or nullptr if out of space.
     */
    void* allocate(size_t bytes) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // Align to 64 bytes for SIMD friendliness
        const size_t aligned_bytes = (bytes + 63) & ~size_t(63);
        
        if (m_offset + aligned_bytes > m_total_bytes) {
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
        m_current_ptr = static_cast<uint8_t*>(m_base_ptr);
        m_offset = 0;
    }

    [[nodiscard]] size_t total_bytes() const { return m_total_bytes; }
    [[nodiscard]] size_t used_bytes() const { return m_offset; }
    [[nodiscard]] double usage_ratio() const { return static_cast<double>(m_offset) / m_total_bytes; }

private:
    void* m_base_ptr{nullptr};
    uint8_t* m_current_ptr{nullptr};
    size_t m_total_bytes{0};
    size_t m_offset{0};
    std::mutex m_mutex;
};

} // namespace chronon3d
