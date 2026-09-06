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
    // DEMOLISHED (P1.4): plan_preallocated removed from the real owner.
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

TEST_SUITE("VulkanSurfaceAuthorityInvariants") {

// Surface-lifetime certification (audit Wave 3): every invariant below pins
// the physical-slot ownership rules implemented by VulkanSurfaceAuthority.
//  - one physical slot → at most one live logical owner;
//  - persistent reservation → cannot be stolen before first initialization;
//  - planned transient → cannot alias a persistent video/NVENC reservation;
//  - logical release → deterministic physical reuse conditions;
//  - encoder ownership (JobPersistent) → explicit, never recycled by
//    transient placement.

namespace {

runtime::SurfaceDesc persistent_desc(std::uint32_t width = 32,
                                     std::uint32_t height = 24) {
    return runtime::SurfaceDesc::make(
        width, height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Generic,
        runtime::LifetimeClass::JobPersistent,
        runtime::ColorMetadata{});
}

} // namespace

TEST_CASE("planned transient cannot alias a persistent reservation before first initialization") {
    // Regression for main@077a442: the old collision check only considered
    // INITIALIZED occupants, so the first planned materialization could steal
    // the physical slot reserved for the video/NVENC ring before that slot
    // had received its first GPU write.  A binding must reserve its slot from
    // the moment it exists, not from its first upload.
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    constexpr runtime::RenderSurfaceHandle persistent_handle{0xC3D401u};
    constexpr runtime::RenderSurfaceHandle transient_handle{0xC3D402u};
    constexpr std::size_t video_ring_slot{4};

    auto persistent = persistent_desc();
    auto transient = test_desc();

    // Persistent video/NVENC reservation. Deliberately NOT initialized: the
    // bug was that an uninitialized reservation looked "free".
    auto& image = authority.bind(persistent_handle, video_ring_slot, persistent);
    (void)image;
    CHECK_FALSE(authority.valid(persistent_handle));

    CHECK_THROWS_AS(
        authority.bind(transient_handle, video_ring_slot, transient),
        std::logic_error);
    CHECK(authority.bound_slot(persistent_handle) == video_ring_slot);
    CHECK(authority.surface_bindings.count(transient_handle) == 0);
    CHECK(owner.make_count == 1);
}

TEST_CASE("unplanned ensure never steals an uninitialized persistent reservation") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    constexpr runtime::RenderSurfaceHandle persistent_handle{0xC3D411u};
    constexpr runtime::RenderSurfaceHandle transient_handle{0xC3D412u};

    auto persistent = persistent_desc();
    auto transient = test_desc();

    authority.ensure(persistent_handle, persistent);
    const auto persistent_slot = authority.bound_slot(persistent_handle);
    CHECK_FALSE(authority.valid(persistent_handle));

    authority.ensure(transient_handle, transient);
    const auto transient_slot = authority.bound_slot(transient_handle);
    CHECK(transient_slot != persistent_slot);
    // The transient must live on its own physical image, never the persistent
    // one (retiring the transient would otherwise clear the encoder ring).
    CHECK(authority.physical_count() == 2);
    CHECK(authority.physical_surfaces.at(transient_slot).desc.lifetime ==
          runtime::LifetimeClass::FrameTransient);
    CHECK(authority.physical_surfaces.at(persistent_slot).desc.lifetime ==
          runtime::LifetimeClass::JobPersistent);
}

TEST_CASE("released transient slot is deterministically reusable after prune") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    constexpr runtime::RenderSurfaceHandle first_handle{0xC3D421u};
    constexpr runtime::RenderSurfaceHandle second_handle{0xC3D422u};
    constexpr std::size_t planned_slot{3};

    auto desc = test_desc();
    authority.bind(first_handle, planned_slot, desc);
    CHECK(authority.physical_count() == 1);

    // Logical release (the Impl drain erases the binding and the transient
    // backing becomes eligible for destruction).
    authority.surface_bindings.erase(first_handle);
    authority.prune_unused_slots();
    CHECK(authority.physical_count() == 0);
    CHECK(owner.destroy_count == 1);

    // Re-bind to the same planned slot: fresh materialization, exact same
    // placement, no resurrected state from the previous occupant.
    auto& image = authority.bind(second_handle, planned_slot, desc);
    CHECK(authority.bound_slot(second_handle) == planned_slot);
    CHECK(authority.physical_count() == 1);
    CHECK(owner.make_count == 2);
    CHECK_FALSE(image.initialized);
}

TEST_CASE("persistent reservation survives release and rebinds to the same slot") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    constexpr runtime::RenderSurfaceHandle handle{0xC3D431u};
    constexpr std::size_t planned_slot{2};

    auto persistent = persistent_desc();
    authority.bind(handle, planned_slot, persistent);
    const auto make_after_first_bind = owner.make_count;

    authority.surface_bindings.erase(handle);
    authority.prune_unused_slots();
    // Encoder/persistent ownership is explicit: prune never reclaims it.
    CHECK(authority.physical_count() == 1);
    CHECK(owner.destroy_count == 0);

    auto& image = authority.bind(handle, planned_slot, persistent);
    CHECK(authority.bound_slot(handle) == planned_slot);
    // Backing storage already exists with matching dimensions: no re-allocate.
    CHECK(owner.make_count == make_after_first_bind);
    CHECK_FALSE(image.initialized);
}

TEST_CASE("ensure transient reuse prefers compatible unused transient slots only") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    constexpr runtime::RenderSurfaceHandle persistent_handle{0xC3D441u};
    constexpr runtime::RenderSurfaceHandle first_handle{0xC3D442u};
    constexpr runtime::RenderSurfaceHandle second_handle{0xC3D443u};

    authority.ensure(persistent_handle, persistent_desc());
    const auto persistent_slot = authority.bound_slot(persistent_handle);
    const auto makes_after_persistent = owner.make_count;

    // First transient occupies a fresh slot.
    auto first = test_desc(32, 24);
    authority.ensure(first_handle, first);
    const auto first_slot = authority.bound_slot(first_handle);
    CHECK(first_slot != persistent_slot);

    // Release the first transient but keep its physical image (unpruned), then
    // ensure() a compatible transient must reuse exactly that slot.
    authority.surface_bindings.erase(first_handle);
    auto second = test_desc(32, 24);
    authority.ensure(second_handle, second);
    CHECK(authority.bound_slot(second_handle) == first_slot);
    CHECK(owner.make_count == makes_after_persistent + 1);  // no new allocation

    // A different-size transient must not displace the compatible slot's
    // occupant or any persistent slot; it moves to a fresh slot instead.
    constexpr runtime::RenderSurfaceHandle third_handle{0xC3D444u};
    auto third = test_desc(64, 48);
    authority.ensure(third_handle, third);
    CHECK(authority.bound_slot(third_handle) != first_slot);
    CHECK(authority.bound_slot(third_handle) != persistent_slot);
    CHECK(authority.physical_surfaces.at(first_slot).desc.lifetime ==
          runtime::LifetimeClass::FrameTransient);
}

TEST_CASE("initialized binding refuses to move to another physical slot") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    constexpr runtime::RenderSurfaceHandle handle{0xC3D451u};
    auto desc = test_desc();

    auto& image = authority.bind(handle, 1, desc);
    image.initialized = true;

    CHECK_THROWS_AS(
        authority.bind(handle, 2, desc),
        std::logic_error);
    CHECK(authority.bound_slot(handle) == 1);
}

} // TEST_SUITE

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
