#pragma once

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <utility>

#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace chronon3d::memory {

// ── Allocation backend discriminant ────────────────────────────────────────

enum class AllocationBackend : u8 {
    MmapHuge,
    MmapStandard,
    Malloc,
    VirtualAlloc,
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
#ifdef __linux__
        case AllocationBackend::MmapHuge:
        case AllocationBackend::MmapStandard:
            ::munmap(data, size);
            break;
#endif
#ifdef _WIN32
        case AllocationBackend::VirtualAlloc:
            ::VirtualFree(data, 0, MEM_RELEASE);
            break;
#endif
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

#ifdef __linux__
    {
        void* ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
        if (ptr != MAP_FAILED) {
            return MemoryBlock(ptr, size, AllocationBackend::MmapHuge);
        }
    }
    {
        void* ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr != MAP_FAILED) {
            return MemoryBlock(ptr, size, AllocationBackend::MmapStandard);
        }
    }
#endif

#ifdef _WIN32
    {
        void* ptr = ::VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (ptr) {
            return MemoryBlock(ptr, size, AllocationBackend::VirtualAlloc);
        }
    }
#endif

    void* ptr = std::malloc(size);
    if (!ptr) return {};
    return MemoryBlock(ptr, size, AllocationBackend::Malloc);
}

inline void free_memory_block(void* ptr, std::size_t size, AllocationBackend backend) noexcept {
    if (!ptr) return;

    switch (backend) {
#ifdef __linux__
    case AllocationBackend::MmapHuge:
    case AllocationBackend::MmapStandard:
        ::munmap(ptr, size);
        return;
#endif
#ifdef _WIN32
    case AllocationBackend::VirtualAlloc:
        ::VirtualFree(ptr, 0, MEM_RELEASE);
        return;
#endif
    case AllocationBackend::Malloc:
    default:
        std::free(ptr);
        return;
    }
}

// ── Legacy compat (used by FramebufferArena which now uses MemoryBlock) ────
// Keep the old names for the transition, but mark them as deprecated redirects.

[[deprecated("Use allocate_memory_block() / MemoryBlock instead")]]
inline void* allocate_huge_pages(size_t size) {
    MemoryBlock block = allocate_memory_block(size);
    if (!block.data) return nullptr;
    // Leak the backend info — caller MUST use the matching free path.
    // Only FramebufferArena still uses this via the compat path; it will
    // be migrated to MemoryBlock in the same commit.
    AllocationBackend* meta = static_cast<AllocationBackend*>(block.data);
    // Cannot store backend info in a raw void* return — this path is
    // fundamentally broken for mixed-backend free.  Callers MUST migrate
    // to MemoryBlock.
    block.data = nullptr;  // prevent double-free
    return static_cast<void*>(meta);
}

[[deprecated("Use free_memory_block() instead")]]
inline void free_huge_pages(void* ptr, size_t size) {
    // This legacy stub cannot know the correct backend.
    // It will be removed once FramebufferArena migrates to MemoryBlock.
    if (!ptr) return;
#ifdef __linux__
    ::munmap(ptr, size);
#elif defined(_WIN32)
    if (!::VirtualFree(ptr, 0, MEM_RELEASE)) {
        std::free(ptr);
    }
#else
    std::free(ptr);
#endif
}

// ── HugePageAllocator — for std::vector<Color, HugePageAllocator<Color>> ───
//
// Embeds the AllocationBackend metadata in the allocated block (before the
// returned pointer) so that deallocate() can dispatch correctly without
// any global state or per-allocation lookup.

template <typename T>
struct HugePageAllocator {
    using value_type = T;

    HugePageAllocator() = default;
    template <class U> constexpr HugePageAllocator(const HugePageAllocator<U>&) noexcept {}
    template <class U> constexpr HugePageAllocator(HugePageAllocator<U>&&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t n) {
        if (n == 0) return nullptr;

        // Overflow guard: n * sizeof(T) must fit in size_t
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_alloc();
        }

        // Pad the metadata slot to alignof(T) so the returned pointer
        // is correctly aligned for T (e.g. Color requires 4-byte alignment).
        constexpr std::size_t meta_pad =
            sizeof(AllocationBackend) > alignof(T)
                ? sizeof(AllocationBackend)
                : alignof(T);

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

        constexpr std::size_t meta_pad =
            sizeof(AllocationBackend) > alignof(T)
                ? sizeof(AllocationBackend)
                : alignof(T);

        auto* raw = static_cast<char*>(static_cast<void*>(p)) - meta_pad;
        auto* meta = reinterpret_cast<AllocationBackend*>(raw);
        const std::size_t total = n * sizeof(T);
        const std::size_t alloc_size = total + meta_pad;

        free_memory_block(raw, alloc_size, *meta);
    }

    bool operator==(const HugePageAllocator&) const = default;
};

} // namespace chronon3d::memory
