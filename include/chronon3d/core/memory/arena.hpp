#pragma once

#include <memory_resource>
#include <stdexcept>
#include <vector>
#include <string>
#include <cstddef>
#include <memory>
#include <cstdint>
#include <chronon3d/core/profiling/profiling_context.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>

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
                     : static_cast<std::pmr::memory_resource*>(&m_counting_resource))) {}

    [[nodiscard]] std::pmr::memory_resource* resource() {
        m_used = true;
        return m_resource.get();
    }

    void reset() {
        m_resource->release();
        m_last_allocation_count = m_allocation_count;
        if (profiling::g_current_counters != nullptr) {
            profiling::g_current_counters->frame_arena_upstream_allocations.fetch_add(
                m_last_allocation_count, std::memory_order_relaxed);
        }
        m_allocation_count = 0;
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
                     : static_cast<std::pmr::memory_resource*>(&m_counting_resource));
    }

    [[nodiscard]] std::byte* base() noexcept { return m_buffer.data(); }
    [[nodiscard]] const std::byte* base() const noexcept { return m_buffer.data(); }
    [[nodiscard]] void* at(size_t offset) noexcept { return base() + offset; }

    [[nodiscard]] bool strict() const noexcept { return m_strict; }
    [[nodiscard]] size_t capacity() const noexcept { return m_buffer.size(); }

    /// Upstream allocation attempts since the last reset. Strict arenas count
    /// an attempted overflow immediately before throwing.
    [[nodiscard]] std::uint64_t allocation_count() const noexcept {
        return m_allocation_count;
    }

    /// Upstream allocation attempts observed by the completed frame.
    [[nodiscard]] std::uint64_t last_allocation_count() const noexcept {
        return m_last_allocation_count;
    }

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
        void* do_allocate(size_t, size_t) override {
            ++m_owner->m_allocation_count;
            throw ArenaOverflow{};
        }
        void do_deallocate(void*, size_t, size_t) override {}
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
            return this == &other;
        }
    public:
        explicit OverflowResource(FrameArena* owner) noexcept : m_owner(owner) {}
    private:
        FrameArena* m_owner;
    } m_overflow_resource{this};

    class CountingResource final : public std::pmr::memory_resource {
    private:
        void* do_allocate(size_t bytes, size_t alignment) override {
            ++m_owner->m_allocation_count;
            return std::pmr::get_default_resource()->allocate(bytes, alignment);
        }
        void do_deallocate(void* ptr, size_t bytes, size_t alignment) override {
            std::pmr::get_default_resource()->deallocate(ptr, bytes, alignment);
        }
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
            return this == &other;
        }
    public:
        explicit CountingResource(FrameArena* owner) noexcept : m_owner(owner) {}
    private:
        FrameArena* m_owner;
    } m_counting_resource{this};

    std::vector<std::byte> m_buffer;
    bool m_strict{false};
    bool m_used{false};
    std::uint64_t m_allocation_count{0};
    std::uint64_t m_last_allocation_count{0};
    std::unique_ptr<std::pmr::monotonic_buffer_resource> m_resource;
};

} // namespace chronon3d
