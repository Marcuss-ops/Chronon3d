#include <doctest/doctest.h>

#ifdef CHRONON3D_ENABLE_VULKAN

#include "../../src/backends/vulkan/vulkan_kernel_store.hpp"
#include "../../src/backends/vulkan/vulkan_pipeline_cache_io.hpp"
#include "../../src/backends/vulkan/vulkan_external_fds.hpp"

#include <array>
#include <cstddef>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <thread>
#include <vector>

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
    CHECK(store.registry.register_kernel(GpuKernelId::TextTileBin, text));

    CHECK(store.registry.resolve(GpuKernelId::Composite) == composite);
    CHECK(store.registry.resolve(GpuKernelId::Blur) == blur);
    CHECK(store.registry.resolve(GpuKernelId::TextTileBin) == text);
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
            if (id == GpuKernelId::TextTileBin && pipeline == text) seen[2] = true;
        });

    CHECK(enumerated == store.registry.size());
    CHECK(enumerated == 3);
    CHECK(seen[0]);
    CHECK(seen[1]);
    CHECK(seen[2]);
}

TEST_CASE("external descriptors close on partial export failure and transfer individually") {
    std::array<int, 3> handles{};
    for (auto& handle : handles) {
        handle = ::open("/dev/null", O_RDONLY);
        REQUIRE(handle >= 0);
    }
    int transferred = -1;
    {
        VulkanExternalFds descriptors(handles[0], handles[1], handles[2]);
        transferred = descriptors.release(1);
        CHECK(transferred == handles[1]);
    }
    CHECK(::fcntl(handles[0], F_GETFD) == -1);
    CHECK(::fcntl(handles[2], F_GETFD) == -1);
    CHECK(::fcntl(transferred, F_GETFD) >= 0);
    CHECK(::close(transferred) == 0);
    int partial = ::open("/dev/null", O_RDONLY);
    REQUIRE(partial >= 0);
    try {
        VulkanExternalFds descriptors(partial);
        throw std::runtime_error("second export failed");
    } catch (const std::runtime_error&) {}
    CHECK(::fcntl(partial, F_GETFD) == -1);
}

TEST_CASE("pipeline cache rejects truncated wrong-device and oversized headers") {
    VkPhysicalDeviceProperties device{};
    device.vendorID = 123;
    device.deviceID = 456;
    device.pipelineCacheUUID[0] = 7;
    std::vector<char> bytes(pipeline_cache_io::kHeaderBytes + 16, 0);
    const std::uint32_t header[]{static_cast<std::uint32_t>(pipeline_cache_io::kHeaderBytes),
        VK_PIPELINE_CACHE_HEADER_VERSION_ONE, device.vendorID, device.deviceID};
    std::memcpy(bytes.data(), header, sizeof(header));
    std::memcpy(bytes.data() + sizeof(header), device.pipelineCacheUUID, VK_UUID_SIZE);
    CHECK(pipeline_cache_io::valid(bytes, device));
    auto other = device;
    ++other.deviceID;
    CHECK_FALSE(pipeline_cache_io::valid(bytes, other));
    other = device;
    ++other.pipelineCacheUUID[0];
    CHECK_FALSE(pipeline_cache_io::valid(bytes, other));
    CHECK_FALSE(pipeline_cache_io::valid(std::span<const char>(bytes.data(), 3), device));
    bytes[0] = 0;
    CHECK_FALSE(pipeline_cache_io::valid(bytes, device));
    CHECK(pipeline_cache_io::filename(device, 11) != pipeline_cache_io::filename(device, 12));
    other = device;
    ++other.driverVersion;
    CHECK(pipeline_cache_io::filename(device, 11) != pipeline_cache_io::filename(other, 11));
}

TEST_CASE("pipeline cache concurrent writers publish only complete blobs") {
    std::string pattern = (std::filesystem::temp_directory_path() / "chronon-cache-test-XXXXXX").string();
    REQUIRE(::mkdtemp(pattern.data()) != nullptr);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
    } cleanup{pattern};
    const auto file = cleanup.path / "cache.bin";
    VkPhysicalDeviceProperties device{};
    device.vendorID = 123;
    device.deviceID = 456;
    std::vector<char> a(pipeline_cache_io::kHeaderBytes + 4096, 'a');
    const std::uint32_t header[]{static_cast<std::uint32_t>(pipeline_cache_io::kHeaderBytes),
        VK_PIPELINE_CACHE_HEADER_VERSION_ONE, device.vendorID, device.deviceID};
    std::memcpy(a.data(), header, sizeof(header));
    std::memcpy(a.data() + sizeof(header), device.pipelineCacheUUID, VK_UUID_SIZE);
    auto b = a;
    std::fill(b.begin() + pipeline_cache_io::kHeaderBytes, b.end(), 'b');
    REQUIRE(pipeline_cache_io::write_atomic(file, a, device));
    std::atomic<bool> okay{true};
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) workers.emplace_back([&, t] {
        for (int i = 0; i < 20; ++i) {
            if (!pipeline_cache_io::write_atomic(file, t % 2 ? a : b, device)) okay = false;
            const auto read = pipeline_cache_io::read(file, device);
            if (read != a && read != b) okay = false;
        }
    });
    for (auto& worker : workers) worker.join();
    CHECK(okay.load());
    for (const auto& entry : std::filesystem::directory_iterator(cleanup.path))
        CHECK(entry.path().filename() == "cache.bin");
    { std::ofstream truncated(file, std::ios::binary | std::ios::trunc); truncated << "bad"; }
    CHECK(pipeline_cache_io::read(file, device).empty());
    { std::ofstream sparse(file, std::ios::binary | std::ios::trunc);
      sparse.seekp(pipeline_cache_io::kMaximumBytes); sparse.put('x'); }
    CHECK(pipeline_cache_io::read(file, device).empty());
    CHECK_FALSE(pipeline_cache_io::write_atomic(file / "not-a-directory", a, device));
    CHECK(pipeline_cache_io::read(cleanup.path / "missing", device).empty());
}

} // TEST_SUITE

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
