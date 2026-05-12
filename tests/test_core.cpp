#include <doctest/doctest.h>
#include <chronon3d/core/types.hpp>

TEST_CASE("Core types check") {
    chronon3d::f32 val = 1.0f;
    CHECK(val == 1.0f);
    
    chronon3d::u32 size = 42;
    CHECK(size == 42);
}
