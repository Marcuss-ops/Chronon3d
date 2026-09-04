#include <doctest/doctest.h>

#ifdef CHRONON3D_ENABLE_VULKAN

#include "../../src/backends/vulkan/vulkan_surface_authority.hpp"

#include <cstdint>
#include <type_traits>

namespace chronon3d::backends::vulkan {
namespace {

template <typename Handle>
Handle fake_vk_handle(std::uintptr_t value) {
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<Handle>(value);
    } else {
        return static_cast<Handle>(value);
    }
}

struct FakeImage {
    VkImage image{VK_NULL_HANDLE};
    std::uint32_t width{0};
    std::uint32_t height{0};
    bool initialized{false};
    runtime::TextAtlasEncoding text_atlas_encoding{
        runtime::TextAtlasEncoding::PremultipliedRGBA};
};

struct FakeSurfaceStats {
    std::uint64_t physical_surfaces_peak{0};
};

struct FakeSurfaceOwner {
    FakeSurfaceStats stats{};
    bool plan_preallocated{false};
    std::uint32_t make_count{0};
    std::uint32_t destroy_count{0};

    void make_image(FakeImage& image,
                    std::uint32_t width,
                    std::uint32_t height,
                    bool,
                    VkFormat) {
        ++make_count;
        image.image = fake_vk_handle<VkImage>(make_count);
        image.width = width;
        image.height = height;
        image.initialized = false;
    }

    void destroy_image(FakeImage& image) noexcept {
        ++destroy_count;
        image = {};
    }

    [[nodiscard]] VkFormat to_vk_format(const runtime::FrameFormat&) const noexcept {
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    }

    void ensure_descriptor_set() noexcept {}

    [[nodiscard]] bool surface_compatible(
        const runtime::SurfaceDesc& lhs,
        const runtime::SurfaceDesc& rhs) const noexcept {
        return lhs.width == rhs.width && lhs.height == rhs.height &&
               lhs.format == rhs.format;
    }
};

runtime::SurfaceDesc test_desc(std::uint32_t width = 32,
                               std::uint32_t height = 24) {
    return runtime::SurfaceDesc::make(
        width, height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Generic,
        runtime::LifetimeClass::FrameTransient,
        runtime::ColorMetadata{});
}

} // namespace

TEST_SUITE("VulkanSurfaceMaterializer") {

TEST_CASE("compiled binding preserves caller-selected physical slot") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;
    authority.next_slot = 99;

    constexpr runtime::RenderSurfaceHandle handle{0xC3D301u};
    constexpr std::size_t planned_slot{7};
    auto desc = test_desc();

    auto& image = authority.bind(handle, planned_slot, desc);

    CHECK(authority.bound_slot(handle) == planned_slot);
    CHECK(authority.next_slot == 99);
    CHECK(authority.physical_count() == 1);
    CHECK(owner.make_count == 1);
    CHECK(image.width == desc.width);
    CHECK(image.height == desc.height);
}

TEST_CASE("materialization changes backing storage but never placement") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;
    authority.next_slot = 41;

    constexpr runtime::RenderSurfaceHandle handle{0xC3D302u};
    constexpr std::size_t planned_slot{5};

    auto first = test_desc(16, 16);
    authority.bind(handle, planned_slot, first);
    CHECK(owner.make_count == 1);

    auto resized = test_desc(64, 16);
    authority.bind(handle, planned_slot, resized);

    CHECK(authority.bound_slot(handle) == planned_slot);
    CHECK(authority.next_slot == 41);
    CHECK(owner.make_count == 2);
    CHECK(owner.destroy_count == 1);
}

TEST_CASE("compiled placement rejects an initialized slot collision") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    constexpr runtime::RenderSurfaceHandle first_handle{0xC3D303u};
    constexpr runtime::RenderSurfaceHandle second_handle{0xC3D304u};
    constexpr std::size_t planned_slot{3};

    auto desc = test_desc();
    auto& image = authority.bind(first_handle, planned_slot, desc);
    image.initialized = true;

    CHECK_THROWS_AS(
        authority.bind(second_handle, planned_slot, desc),
        std::logic_error);
    CHECK(authority.bound_slot(first_handle) == planned_slot);
    CHECK(authority.surface_bindings.count(second_handle) == 0);
}

} // TEST_SUITE

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
