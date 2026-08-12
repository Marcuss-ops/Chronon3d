#pragma once

#include <memory_resource>
#include <stdexcept>
#include <vector>
#include <string>

namespace chronon3d {

class ArenaOverflow final : public std::runtime_error {
public:
    ArenaOverflow() : std::runtime_error("FrameArena strict capacity exhausted") {}
};

/**
 * @brief A simple per-frame memory arena using std::pmr::monotonic_buffer_resource.
 * 
 * This is designed to be used in the hot path (evaluation and rendering) to avoid
 * costly heap allocations. The memory is released all at once at the end of the frame.
 */
class FrameArena {
public:
    explicit FrameArena(size_t initial_size = 1024 * 1024,
                        bool strict = false) // 1MB default
        : m_buffer(initial_size)
        , m_strict(strict)
        , m_resource(m_buffer.data(), m_buffer.size(),
                     m_strict ? static_cast<std::pmr::memory_resource*>(&m_overflow_resource)
                              : std::pmr::get_default_resource()) {}

    [[nodiscard]] std::pmr::memory_resource* resource() {
        return &m_resource;
    }

    void reset() {
        m_resource.release();
    }

    [[nodiscard]] bool strict() const noexcept { return m_strict; }
    [[nodiscard]] size_t capacity() const noexcept { return m_buffer.size(); }

private:
    class OverflowResource final : public std::pmr::memory_resource {
    private:
        void* do_allocate(size_t, size_t) override { throw ArenaOverflow{}; }
        void do_deallocate(void*, size_t, size_t) override {}
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
            return this == &other;
        }
    } m_overflow_resource;
    std::vector<std::byte> m_buffer;
    bool m_strict{false};
    std::pmr::monotonic_buffer_resource m_resource;
};

} // namespace chronon3d
