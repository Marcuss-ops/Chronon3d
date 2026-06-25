#pragma once

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

#include <sys/mman.h>
#include <unistd.h>

namespace chronon3d::memory {

// ── Allocation backend discriminant ────────────────────────────────────────

enum class AllocationBackend : u8 {
    MmapHuge,
    MmapStandard,
    Malloc,
};

// ── RAII MemoryBlock — remembers how it was allocated so free is correct ───

struct MemoryBlock {
    void* data{nullptr};
    std::size_t size{0};
    AllocationBackend backend{AllocationBackend::Malloc};

    MemoryBlock() = default;

    MemoryBlock(void* d, std::size_t s, AllocationBackend b) noexcept
        : data(d), size(s), backend(b) {}

    // No copy
    MemoryBlock(const MemoryBlock&) = delete;
    MemoryBlock& operator=(const MemoryBlock&) = delete;

    // Move
    MemoryBlock(MemoryBlock&& other) noexcept
        : data(other.data), size(other.size), backend(other.backend) {
        other.data = nullptr;
        other.size = 0;
    }

    MemoryBlock& operator=(MemoryBlock&& other) noexcept {
        if (this != &other) {
            destroy();
            data = other.data;
            size = other.size;
            backend = other.backend;
            other.data = nullptr;
            other.size = 0;
        }
        return *this;
    }

    ~MemoryBlock() { destroy(); }

    [[nodiscard]] explicit operator bool() const noexcept { return data != nullptr; }

    void destroy() noexcept {
        if (!data) return;

        switch (backend) {
        case AllocationBackend::MmapHuge:
        case AllocationBackend::MmapStandard:
            ::munmap(data, size);
            break;
        case AllocationBackend::Malloc:
        default:
            std::free(data);
            break;
        }

        data = nullptr;
        size = 0;
    }
};

// ── Public API: allocate + free with correct dispatch ──────────────────────

/**
 * @brief Allocates memory, preferring huge pages.
 *
 * Tries (in order):
 *   1. mmap(MAP_HUGETLB)      → backend = MmapHuge
 *   2. mmap (standard pages)  → backend = MmapStandard
 *   3. (Windows) VirtualAlloc → backend = VirtualAlloc
 *   4. std::malloc            → backend = Malloc
 *
 * Returns an empty MemoryBlock (data == nullptr) on failure.
 */
inline MemoryBlock allocate_memory_block(std::size_t size) {
    if (size == 0) return {};

    constexpr std::size_t huge_page_size = static_cast<std::size_t>(2) * 1024 * 1024;
    const std::size_t aligned_size =
        (size + huge_page_size - 1) & ~(huge_page_size - 1);
    {
        void* ptr = ::mmap(nullptr, aligned_size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
        if (ptr != MAP_FAILED) {
            (void)::madvise(ptr, aligned_size, MADV_HUGEPAGE);
            return MemoryBlock(ptr, aligned_size, AllocationBackend::MmapHuge);
        }
    }
    {
        void* ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr != MAP_FAILED) {
            (void)::madvise(ptr, size, MADV_HUGEPAGE);
            return MemoryBlock(ptr, size, AllocationBackend::MmapStandard);
        }
    }

    void* ptr = std::malloc(size);
    if (!ptr) return {};
    return MemoryBlock(ptr, size, AllocationBackend::Malloc);
}

inline void free_memory_block(void* ptr, std::size_t size, AllocationBackend backend) noexcept {
    if (!ptr) return;

    switch (backend) {
    case AllocationBackend::MmapHuge:
    case AllocationBackend::MmapStandard:
        ::munmap(ptr, size);
        return;
    case AllocationBackend::Malloc:
    default:
        std::free(ptr);
        return;
    }
}

// ── HugePageAllocator — for std::vector<Color, HugePageAllocator<Color>> ───
//
// Embeds the AllocationBackend metadata in the allocated block (before the
// returned pointer) so that deallocate() can dispatch correctly without
// any global state or per-allocation lookup.  The metadata pad is rounded
// up to a 64-byte boundary so the returned pointer is always cache-line
// aligned — required by `Framebuffer`'s stride alignment contract for
// SIMD / cache-friendly row access.
//
// `k_huge_page_header<T>()` is the SINGLE source of truth for the metadata
// pad (both `allocate()` and `deallocate()` reference it; a previous
// inline-constexpr-block duplication risked silent drift on edit).

template <typename T>
constexpr std::size_t k_huge_page_header() noexcept {
    constexpr std::size_t base_pad =
        sizeof(AllocationBackend) > alignof(T)
            ? sizeof(AllocationBackend)
            : alignof(T);
    constexpr std::size_t meta_pad =
        (base_pad + static_cast<std::size_t>(63)) & ~static_cast<std::size_t>(63);
    static_assert(meta_pad >= 64 && meta_pad % 64 == 0,
                  "HugePageAllocator meta_pad must be at least one 64-byte cache line");
    return meta_pad;
}

template <typename T>
struct HugePageAllocator {
    using value_type = T;
    // Stateless allocator: declare the STL traits so containers can swap
    // / move-assign in O(1) (pointer swap) instead of paying the O(N)
    // element-by-element copy a stateful default would force.
    using is_always_equal = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;
    using propagate_on_container_copy_assignment = std::false_type;

    HugePageAllocator() = default;
    template <class U> constexpr HugePageAllocator(const HugePageAllocator<U>&) noexcept {}
    template <class U> constexpr HugePageAllocator(HugePageAllocator<U>&&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t n) {
        if (n == 0) return nullptr;

        // Overflow guard: n * sizeof(T) must fit in size_t
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_alloc();
        }

        // Pad the metadata slot to a 64-byte boundary so the returned
        // pointer is cache-line aligned.  The exact meta_pad size lives
        // in `k_huge_page_header<T>()` (single source of truth).
        constexpr std::size_t meta_pad = k_huge_page_header<T>();

        const std::size_t total = n * sizeof(T);
        const std::size_t alloc_size = total + meta_pad;

        MemoryBlock block = allocate_memory_block(alloc_size);
        if (!block.data) throw std::bad_alloc();

        // Store backend at the start of the block, return aligned pointer
        auto* meta = static_cast<AllocationBackend*>(block.data);
        *meta = block.backend;

        // Release the MemoryBlock — ownership transferred to deallocate()
        block.data = nullptr;

        return reinterpret_cast<T*>(
            static_cast<char*>(static_cast<void*>(meta)) + meta_pad);
    }

    void deallocate(T* p, std::size_t n) noexcept {
        if (!p) return;

        // Mirror the offset used by `allocate()` exactly so `raw - meta_pad`
        // resolves to the same base pointer.  `k_huge_page_header<T>()`
        // is the canonical helper; do NOT inline a second copy of the
        // rounding math here.
        constexpr std::size_t meta_pad = k_huge_page_header<T>();

        auto* raw = static_cast<char*>(static_cast<void*>(p)) - meta_pad;
        auto* meta = reinterpret_cast<AllocationBackend*>(raw);
        const std::size_t total = n * sizeof(T);
        const std::size_t alloc_size = total + meta_pad;

        free_memory_block(raw, alloc_size, *meta);
    }

    bool operator==(const HugePageAllocator&) const = default;
};

} // namespace chronon3d::memory
