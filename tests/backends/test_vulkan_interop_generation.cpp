#include <doctest/doctest.h>

#include <chronon3d/backends/vulkan/vulkan_backend.hpp>

#include <memory>

using chronon3d::backends::vulkan::InteropGeneration;

TEST_CASE("InteropGeneration owns independent monotonic Vulkan/CUDA values") {
    auto generation = std::make_shared<InteropGeneration>();

    CHECK(generation->vulkan_to_cuda() == 0);
    CHECK(generation->cuda_to_vulkan() == 0);

    CHECK(generation->next_vulkan_to_cuda() == 1);
    CHECK(generation->next_vulkan_to_cuda() == 2);
    CHECK(generation->vulkan_to_cuda() == 2);
    CHECK(generation->cuda_to_vulkan() == 0);

    CHECK(generation->next_cuda_to_vulkan() == 1);
    CHECK(generation->next_cuda_to_vulkan() == 2);
    CHECK(generation->cuda_to_vulkan() == 2);
    CHECK(generation->vulkan_to_cuda() == 2);
}

TEST_CASE("InteropGeneration survives bridge recreation through shared ownership") {
    auto authority = std::make_shared<InteropGeneration>();
    CHECK(authority->next_vulkan_to_cuda() == 1);

    // A recreated CUDA bridge receives the same authority, never a local
    // counter. The next signal must continue the sequence.
    auto recreated_bridge_authority = authority;
    CHECK(recreated_bridge_authority->next_vulkan_to_cuda() == 2);
    CHECK(authority->vulkan_to_cuda() == 2);
}
