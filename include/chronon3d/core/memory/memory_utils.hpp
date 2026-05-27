#pragma once

#include <cstddef>
#include <cstdlib>
#include <iostream>

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

/**
 * @brief Tries to allocate memory using huge pages (2MB or larger) if supported.
 * Falls back to standard allocation if huge pages are not available or fail.
 */
inline void* allocate_huge_pages(size_t size) {
#ifdef __linux__
    // Try to allocate using mmap with HugeTLB
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, 
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (ptr != MAP_FAILED) {
        return ptr;
    }
    // Fallback to standard page mmap under Linux (safe to munmap)
    ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, 
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr != MAP_FAILED) {
        return ptr;
    }
#endif

#ifdef _WIN32
    // Windows large pages require SE_LARGE_PAGE_NAME privilege which is often not granted.
    // For now, we use VirtualAlloc and if we really want large pages we need to adjust token privileges.
    // Simplifying to standard VirtualAlloc for now or trying large pages.
    void* ptr = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (ptr) return ptr;
#endif

    return std::malloc(size);
}

inline void free_huge_pages(void* ptr, size_t size) {
    if (!ptr) return;
#ifdef __linux__
    (void)munmap(ptr, size);
    return;
#endif

#ifdef _WIN32
    if (VirtualFree(ptr, 0, MEM_RELEASE)) return;
#endif

    std::free(ptr);
}

template <typename T>
struct HugePageAllocator {
    using value_type = T;
    HugePageAllocator() = default;
    template <class U> constexpr HugePageAllocator(const HugePageAllocator<U>&) noexcept {}
    
    [[nodiscard]] T* allocate(std::size_t n) {
        if (n == 0) return nullptr;
        void* p = allocate_huge_pages(n * sizeof(T));
        if (!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    
    void deallocate(T* p, std::size_t n) noexcept {
        free_huge_pages(p, n * sizeof(T));
    }

    bool operator==(const HugePageAllocator&) const = default;
};

} // namespace chronon3d::memory
