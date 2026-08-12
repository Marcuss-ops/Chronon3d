#include <doctest/doctest.h>

#include <chronon3d/core/memory/arena.hpp>

#include <cstddef>
#include <memory_resource>

TEST_CASE("FrameArena strict mode rejects upstream allocation") {
    chronon3d::FrameArena arena(64, true);

    CHECK(arena.strict());
    CHECK(arena.capacity() == 64);
    CHECK_NOTHROW([&] {
        void* memory = arena.resource()->allocate(32, alignof(std::max_align_t));
        arena.resource()->deallocate(memory, 32, alignof(std::max_align_t));
    }());
    CHECK_THROWS_AS(
        static_cast<void>(arena.resource()->allocate(128, alignof(std::max_align_t))),
        chronon3d::ArenaOverflow);
}

TEST_CASE("FrameArena default mode retains compatibility upstream") {
    chronon3d::FrameArena arena(64);

    CHECK_FALSE(arena.strict());
    CHECK_NOTHROW([&] {
        void* memory = arena.resource()->allocate(128, alignof(std::max_align_t));
        arena.resource()->deallocate(memory, 128, alignof(std::max_align_t));
    }());
}
