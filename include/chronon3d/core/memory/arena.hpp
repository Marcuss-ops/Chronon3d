#pragma once

#include <memory_resource>
#include <stdexcept>
#include <vector>
#include <string>
#include <cstddef>
#include <memory>

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
        , m_resource(std::make_unique<std::pmr::monotonic_buffer_resource>(
              m_buffer.data(), m_buffer.size(),
              m_strict ? static_cast<std::pmr::memory_resource*>(&m_overflow_resource)
                       : std::pmr::get_default_resource())) {}

    [[nodiscard]] std::pmr::memory_resource* resource() {
        m_used = true;
        return m_resource.get();
    }

    void reset() {
        m_resource->release();
        m_used = false;
    }

    /// Reserve the backing store during prepare/compile.  Rendering code can
    /// then use base()+offset without calling the upstream allocator.
    void reserve(size_t required) {
        if (required <= m_buffer.size()) return;
        if (m_resource_used()) {
            throw std::logic_error("FrameArena::reserve must run before use");
        }
        m_buffer.resize(required);
        m_resource = std::make_unique<std::pmr::monotonic_buffer_resource>(
            m_buffer.data(), m_buffer.size(),
            m_strict ? static_cast<std::pmr::memory_resource*>(&m_overflow_resource)
                     : std::pmr::get_default_resource());
    }

    [[nodiscard]] std::byte* base() noexcept { return m_buffer.data(); }
    [[nodiscard]] const std::byte* base() const noexcept { return m_buffer.data(); }
    [[nodiscard]] void* at(size_t offset) noexcept { return base() + offset; }

    [[nodiscard]] bool strict() const noexcept { return m_strict; }
    [[nodiscard]] size_t capacity() const noexcept { return m_buffer.size(); }

private:
    [[nodiscard]] bool m_resource_used() const noexcept {
        // monotonic_buffer_resource has no portable "used" query.  A
        // reservation larger than the initial store is therefore only
        // supported before the first reset/allocating operation; callers use
        // this method as a conservative guard via the explicit flag below.
        return m_used;
    }

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
    bool m_used{false};
    std::unique_ptr<std::pmr::monotonic_buffer_resource> m_resource;
};

} // namespace chronon3d
