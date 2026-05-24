#pragma once

#include <chronon3d/core/framebuffer_arena.hpp>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>

namespace chronon3d {

/**
 * @brief Manages multiple FramebufferArenas to support concurrent rendering and encoding.
 * 
 * P1: Triple Buffering avoids race conditions when the renderer needs a new arena
 * while FFmpeg is still reading from the previous one.
 */
class TripleBufferArena {
public:
    TripleBufferArena(size_t pool_count, size_t arena_size) {
        for (size_t i = 0; i < pool_count; ++i) {
            m_arenas.push_back(std::make_shared<FramebufferArena>(arena_size));
            m_in_use.push_back(false);
        }
    }

    /**
     * @brief Acquires an available arena for the render thread.
     * Blocks if no arena is available.
     */
    std::shared_ptr<FramebufferArena> acquire() {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (;;) {
            for (size_t i = 0; i < m_arenas.size(); ++i) {
                if (!m_in_use[i]) {
                    m_in_use[i] = true;
                    m_arenas[i]->reset();
                    return m_arenas[i];
                }
            }
            // All arenas in use, wait for one to be released
            // (In a real implementation, we'd use a condition variable)
            lock.unlock();
            std::this_thread::yield();
            lock.lock();
        }
    }

    /**
     * @brief Releases an arena back to the pool.
     */
    void release(const std::shared_ptr<FramebufferArena>& arena) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < m_arenas.size(); ++i) {
            if (m_arenas[i] == arena) {
                m_in_use[i] = false;
                return;
            }
        }
    }

private:
    std::vector<std::shared_ptr<FramebufferArena>> m_arenas;
    std::vector<bool> m_in_use;
    std::mutex m_mutex;
};

} // namespace chronon3d
