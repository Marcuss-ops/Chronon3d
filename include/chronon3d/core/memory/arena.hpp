#pragma once

#include <memory_resource>
#include <vector>
#include <string>

namespace chronon3d {

/**
 * @brief A simple per-frame memory arena using std::pmr::monotonic_buffer_resource.
 * 
 * This is designed to be used in the hot path (evaluation and rendering) to avoid
 * costly heap allocations. The memory is released all at once at the end of the frame.
 */
class FrameArena {
public:
    explicit FrameArena(size_t initial_size = 1024 * 1024) // 1MB default
        : m_buffer(initial_size)
        , m_resource(m_buffer.data(), m_buffer.size()) {}

    [[nodiscard]] std::pmr::memory_resource* resource() {
        return &m_resource;
    }

    void reset() {
        m_resource.release();
    }

    // Helper types for PMR containers
    template <typename T>
    using vector = std::pmr::vector<T>;

    using string = std::pmr::string;

private:
    std::vector<std::byte> m_buffer;
    std::pmr::monotonic_buffer_resource m_resource;
};

} // namespace chronon3d
