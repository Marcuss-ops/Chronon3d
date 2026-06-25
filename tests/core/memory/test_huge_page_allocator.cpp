// tests/core/memory/test_huge_page_allocator.cpp
//
// Blocco 2 — HugePageAllocator alignment invariant (PERF BOTTLENECKS review P0).
//
// The allocator embeds a small backend-metadata header BEFORE the returned
// pointer.  To honour `Framebuffer`'s cache-line stride-alignment contract
// the metadata pad is rounded up to 64 bytes so the pointer handed back to
// the user is always 64-byte aligned regardless of which backend (mmap huge,
// mmap standard, malloc) satisfied the request.
//
// These tests are the mandated invariant: the returned pointer must satisfy
// `reinterpret_cast<std::uintptr_t>(p) % 64 == 0` for every call, every
// size, every cycle.  The specific backend path is intentionally opaque
// (the allocator doesn't expose which it picked), so the test exercises the
// alignment invariant by sweeping sizes that almost always touch each
// backend at least once across repeated alloc/dealloc cycles.

#include <doctest/doctest.h>

#include <chronon3d/core/memory/memory_utils.hpp>
#include <chronon3d/math/color.hpp>

#include <cstdint>
#include <vector>

namespace {

constexpr std::size_t k_cacheline = 64;

[[nodiscard]] inline bool is_64_aligned(const void* p) noexcept {
    return (reinterpret_cast<std::uintptr_t>(p) % k_cacheline) == 0;
}

// Sizes that cover the whole span of fallback paths:
//   - small (likely malloc)
//   - 64 KiB / 4 MiB (likely standard mmap)
//   - 1080p / 4K (likely huge-page mmap when available)
constexpr std::size_t k_sweep_sizes[] = {
    64,
    1024,
    65536,
    4ULL * 1024 * 1024,
    static_cast<std::size_t>(1920) * 1080,
    static_cast<std::size_t>(3840) * 2160,
};

} // namespace

TEST_CASE("HugePageAllocator<int>: 64-byte alignment across small / medium / 1080p / 4K") {
    using Alloc = chronon3d::memory::HugePageAllocator<int>;

    SUBCASE("Small buffer (256 elements)") {
        std::vector<int, Alloc> v(256);
        REQUIRE(v.data() != nullptr);
        CHECK(is_64_aligned(v.data()));
    }

    SUBCASE("Medium buffer (1080p element count)") {
        std::vector<int, Alloc> v(static_cast<std::size_t>(1920) * 1080);
        REQUIRE(v.data() != nullptr);
        CHECK(is_64_aligned(v.data()));
    }

    SUBCASE("4K equivalent (3840x2160 elements ≈ 33.2 MiB)") {
        std::vector<int, Alloc> v(static_cast<std::size_t>(3840) * 2160);
        REQUIRE(v.data() != nullptr);
        CHECK(is_64_aligned(v.data()));
    }
}

TEST_CASE("HugePageAllocator<Color>: 64-byte alignment for the Framebuffer-canonical use case") {
    using Alloc = chronon3d::memory::HugePageAllocator<chronon3d::Color>;

    SUBCASE("640x360 (SD framebuffer)") {
        std::vector<chronon3d::Color, Alloc> v(static_cast<std::size_t>(640) * 360);
        REQUIRE(v.data() != nullptr);
        CHECK(is_64_aligned(v.data()));
    }

    SUBCASE("1920x1080 (Full HD framebuffer)") {
        std::vector<chronon3d::Color, Alloc> v(static_cast<std::size_t>(1920) * 1080);
        REQUIRE(v.data() != nullptr);
        CHECK(is_64_aligned(v.data()));
    }

    SUBCASE("3840x2160 (4K framebuffer)") {
        std::vector<chronon3d::Color, Alloc> v(static_cast<std::size_t>(3840) * 2160);
        REQUIRE(v.data() != nullptr);
        CHECK(is_64_aligned(v.data()));
    }
}

TEST_CASE("HugePageAllocator<int>: repeated alloc/dealloc cycles hold alignment across all backends") {
    using Alloc = chronon3d::memory::HugePageAllocator<int>;

    // Exercise each size N times.  Even when the platform's huge-page pool
    // is exhausted and small/mmap/malloc fallbacks are intermingled, every
    // call must honour the 64-byte invariant.
    for (int cycle = 0; cycle < 4; ++cycle) {
        for (std::size_t n : k_sweep_sizes) {
            std::vector<int, Alloc> v(n);
            REQUIRE(v.data() != nullptr);
            CHECK(is_64_aligned(v.data()));
            // Touch front + back to confirm the mapping is actually valid.
            v.front() = static_cast<int>(cycle);
            v.back()  = static_cast<int>(n);
        }
    }
}

TEST_CASE("HugePageAllocator counter-offer: k_huge_page_header<int>() is >= 64 and 64-aligned") {
    constexpr std::size_t pad_int  = chronon3d::memory::k_huge_page_header<int>();
    constexpr std::size_t pad_col  = chronon3d::memory::k_huge_page_header<chronon3d::Color>();
    CHECK(pad_int  >= 64);
    CHECK(pad_int  % 64 == 0);
    CHECK(pad_col  >= 64);
    CHECK(pad_col  % 64 == 0);
    // Compile-time check baked into the helper itself — this is the
    // defensive guarantee in case a downstream refactor accidentally drops
    // the rounding math.
    static_assert(chronon3d::memory::k_huge_page_header<int>() >= 64);
    static_assert(chronon3d::memory::k_huge_page_header<chronon3d::Color>() >= 64);
}
