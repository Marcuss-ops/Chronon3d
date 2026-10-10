#include <doctest/doctest.h>

#include <chronon3d/core/hash/hash_primitives.hpp>

TEST_CASE("SplitMix64 helpers preserve the finalizer and sequence step") {
    CHECK(chronon3d::core::hash::splitmix64_finalize(0x123456789abcdef0ULL) ==
          0x9629f58e8ec5b906ULL);
    CHECK(chronon3d::core::hash::splitmix64(0x123456789abcdef0ULL) ==
          0x161922c645ce50e8ULL);
}
