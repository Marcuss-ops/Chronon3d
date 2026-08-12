#include <doctest/doctest.h>

#include <chronon3d/runtime/bounded_spsc_ring.hpp>

TEST_CASE("BoundedSpscRing is fixed-capacity and preserves FIFO order") {
    chronon3d::runtime::BoundedSpscRing<int, 3> ring;

    CHECK(ring.capacity() == 3);
    CHECK(ring.empty());
    CHECK(ring.try_push(10));
    CHECK(ring.try_push(20));
    CHECK(ring.try_push(30));
    CHECK_FALSE(ring.try_push(40));
    CHECK(ring.full());

    int value = 0;
    REQUIRE(ring.try_pop(value));
    CHECK(value == 10);
    REQUIRE(ring.try_pop(value));
    CHECK(value == 20);
    CHECK(ring.try_push(40));
    REQUIRE(ring.try_pop(value));
    CHECK(value == 30);
    REQUIRE(ring.try_pop(value));
    CHECK(value == 40);
    CHECK_FALSE(ring.try_pop(value));
    CHECK(ring.empty());
}

TEST_CASE("BoundedSpscRing has no dynamic queue growth") {
    chronon3d::runtime::BoundedSpscRing<std::size_t, 2> ring;
    CHECK(ring.size() == 0);
    CHECK(ring.try_push(1));
    CHECK(ring.try_push(2));
    CHECK_FALSE(ring.try_push(3));
    CHECK(ring.size() == 2);
}
