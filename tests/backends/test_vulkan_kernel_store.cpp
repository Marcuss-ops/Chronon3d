#include <doctest/doctest.h>

#ifdef CHRONON3D_ENABLE_VULKAN

#include "../../src/backends/vulkan/vulkan_kernel_store.hpp"

#include <array>
#include <cstddef>

namespace chronon3d::backends::vulkan {

TEST_SUITE("VulkanKernelStore") {

TEST_CASE("pipeline cache has one explicit owner and starts empty") {
    VulkanKernelStore store;

    CHECK(store.pipeline_cache == VK_NULL_HANDLE);
}

TEST_CASE("registry is the sole kernel to pipeline resolution authority") {
    VulkanKernelStore store;

    constexpr auto composite = static_cast<GpuKernelRegistry::PipelineHandle>(0x101u);
    constexpr auto blur = static_cast<GpuKernelRegistry::PipelineHandle>(0x202u);
    constexpr auto text = static_cast<GpuKernelRegistry::PipelineHandle>(0x303u);

    CHECK(store.registry.register_kernel(GpuKernelId::Composite, composite));
    CHECK(store.registry.register_kernel(GpuKernelId::Blur, blur));
    CHECK(store.registry.register_kernel(GpuKernelId::TextBatch, text));

    CHECK(store.registry.resolve(GpuKernelId::Composite) == composite);
    CHECK(store.registry.resolve(GpuKernelId::Blur) == blur);
    CHECK(store.registry.resolve(GpuKernelId::TextBatch) == text);
    CHECK(store.registry.resolve(GpuKernelId::Matte) == 0);

    // A second registration for the same semantic kernel must not create a
    // parallel mapping or silently replace the registry authority.
    CHECK_FALSE(store.registry.register_kernel(
        GpuKernelId::Composite,
        static_cast<GpuKernelRegistry::PipelineHandle>(0x404u)));
    CHECK(store.registry.resolve(GpuKernelId::Composite) == composite);

    std::array<bool, 3> seen{};
    std::size_t enumerated = 0;
    store.registry.for_each_registered(
        [&](GpuKernelId id, GpuKernelRegistry::PipelineHandle pipeline) {
            ++enumerated;
            if (id == GpuKernelId::Composite && pipeline == composite) seen[0] = true;
            if (id == GpuKernelId::Blur && pipeline == blur) seen[1] = true;
            if (id == GpuKernelId::TextBatch && pipeline == text) seen[2] = true;
        });

    CHECK(enumerated == store.registry.size());
    CHECK(enumerated == 3);
    CHECK(seen[0]);
    CHECK(seen[1]);
    CHECK(seen[2]);
}

} // TEST_SUITE

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
