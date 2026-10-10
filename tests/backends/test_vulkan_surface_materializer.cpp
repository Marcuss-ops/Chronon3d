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

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
TEST_CASE("CUDA handoffs remain pending until their own surface is recorded") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    authority.cuda_pending_surfaces.insert(11);
    authority.cuda_pending_surfaces.insert(12);

    CHECK_FALSE(authority.acquire_pending_cuda_surface(10));
    CHECK(authority.cuda_ready_surfaces.empty());
    CHECK(authority.acquire_pending_cuda_surface(11));
    CHECK_FALSE(authority.acquire_pending_cuda_surface(11));
    CHECK(authority.cuda_ready_surfaces.size() == 1);
    CHECK(authority.cuda_ready_surfaces.contains(11));
    CHECK(authority.cuda_pending_surfaces.contains(12));

    // Submitting another ring slot must not enqueue a spurious V2C frame
    // signal for slot 12 or discard its unconsumed CUDA ownership token.
    authority.cuda_ready_surfaces.clear();
    CHECK(authority.acquire_pending_cuda_surface(12));
    CHECK(authority.cuda_pending_surfaces.empty());
    CHECK(authority.cuda_ready_surfaces.contains(12));
}
#endif

TEST_CASE("global transient cleanup preserves every in-flight surface") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    constexpr runtime::RenderSurfaceHandle pending{901}, complete{902};
    authority.note_recording_slot(pending, 1);
    authority.note_recording_slot(pending, 5);
    authority.note_recording_slot(complete, 2);
    std::array<bool, 6> busy{};
    busy[5] = true;
    CHECK_FALSE(authority.can_retire_surface(pending, busy));
    CHECK(authority.can_retire_surface(complete, busy));
    busy[5] = false;
    CHECK(authority.can_retire_surface(pending, busy));
}

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
    CHECK(authority.retained_bindings_high_water == 1);
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

TEST_CASE("surface lifecycle high-water counters track enqueue and binding peaks") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    constexpr runtime::RenderSurfaceHandle first{0xC3D305u};
    constexpr runtime::RenderSurfaceHandle second{0xC3D306u};
    const auto desc = test_desc();
    authority.bind(first, 1, desc);
    authority.bind(second, 2, desc);
    CHECK(authority.retained_bindings_high_water == 2);

    // A deferred release queue peak is cumulative high-water, not the current
    // queue depth; draining it later must not erase the observation.
    authority.enqueue_deferred_release({first, 0});
    authority.enqueue_deferred_release({second, 1});
    CHECK(authority.deferred_release_count() == 2);
    CHECK(authority.pending_release_high_water == 2);
    authority.deferred_surface_releases.clear();
    CHECK(authority.deferred_release_count() == 0);
    CHECK(authority.pending_release_high_water == 2);

    authority.note_release_delay(10, 10);
    authority.note_release_delay(7, 10);
    authority.note_release_delay(12, 10); // Defensive: impossible future request.
    CHECK(authority.release_delay_frames == 3);
    CHECK(authority.release_delay_frames_high_water == 3);
    authority.note_release_delay(1, 10);
    CHECK(authority.release_delay_frames == 12);
    CHECK(authority.release_delay_frames_high_water == 9);

    // Removing a binding likewise cannot lower its historical high-water.
    authority.surface_bindings.erase(second);
    CHECK(authority.binding_count() == 1);
    CHECK(authority.retained_bindings_high_water == 2);

    CHECK_FALSE(authority.note_overflow_if_needed(2));
    authority.enqueue_deferred_release({first, 0});
    authority.enqueue_deferred_release({second, 1});
    authority.enqueue_deferred_release({first, 2});
    CHECK(authority.note_overflow_if_needed(2));
    CHECK(authority.note_overflow_if_needed(1));
    CHECK(authority.overflow_wait_count == 2);
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
    // ``valid()`` reports MATERIALIZATION, not content (deliberate: d1f43877a
    // switched it from ``initialized`` to a non-null image to fix the
    // Vulkan-CUDA interop hang). The precondition this case needs is still
    // "reservation exists but has no content yet", so assert the flag itself.
    CHECK(authority.valid(persistent_handle));
    CHECK_FALSE(
        authority.physical_surfaces.at(video_ring_slot).image.initialized);

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
    // Materialized but deliberately uninitialized (see the note above on the
    // post-d1f43877a meaning of ``valid()``).
    CHECK(authority.valid(persistent_handle));
    CHECK_FALSE(
        authority.physical_surfaces.at(persistent_slot).image.initialized);

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

TEST_CASE("released transient slot stays warm and is deterministically reusable") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    constexpr runtime::RenderSurfaceHandle first_handle{0xC3D421u};
    constexpr runtime::RenderSurfaceHandle second_handle{0xC3D422u};
    constexpr std::size_t planned_slot{3};

    auto desc = test_desc();
    authority.bind(first_handle, planned_slot, desc);
    CHECK(authority.physical_count() == 1);

    // Logical release (the Impl drain erases the binding). The bounded warm
    // pool retains the physical image so the next frame does not recreate it.
    authority.surface_bindings.erase(first_handle);
    authority.prune_unused_slots();
    CHECK(authority.physical_count() == 1);
    CHECK(owner.destroy_count == 0);

    // Re-bind to the same planned slot: exact placement and no new image
    // materialization. The command-plan transition owns the first write of
    // the next frame, so the previous initialized bit is not a correctness
    // gate here.
    auto& image = authority.bind(second_handle, planned_slot, desc);
    CHECK(authority.bound_slot(second_handle) == planned_slot);
    CHECK(authority.physical_count() == 1);
    CHECK(owner.make_count == 1);
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

TEST_CASE("initialized persistent binding refuses to move to another physical slot") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    constexpr runtime::RenderSurfaceHandle handle{0xC3D451u};
    auto desc = persistent_desc();

    auto& image = authority.bind(handle, 1, desc);
    image.initialized = true;

    // Persistent / initialized external content is never silently replaced by
    // a plan re-placement.
    CHECK_THROWS_AS(
        authority.bind(handle, 2, desc),
        std::logic_error);
    CHECK(authority.bound_slot(handle) == 1);
}

TEST_CASE("initialized transient binding is re-slot by content-aware placement") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    constexpr runtime::RenderSurfaceHandle handle{0xC3D452u};
    auto desc = test_desc();

    auto& image = authority.bind(handle, 1, desc);
    image.initialized = true;

    // The plan is the placement authority: a FrameTransient binding may be
    // re-placed once its content has retired ("make Vulkan planned placement
    // content aware"). Only persistent/initialized external content is pinned.
    auto& moved = authority.bind(handle, 2, desc);
    (void)moved;
    CHECK(authority.bound_slot(handle) == 2);
}

// ── Audit N4: recording lease + generation token ─────────────────────────
//
// These cases are the contract the decoupled recording path relies on: the
// store lock may be released after the lookup because a leased slot can
// neither be pruned (image destroyed) nor re-materialized, and the token
// makes a violation observable instead of a silent use-after-free.

TEST_CASE("a leased slot survives prune_unused_slots") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    // 70 unbound transient physical slots: 6 beyond the retention budget, so
    // prune has real work to do.
    constexpr std::size_t kSlots = 70;
    std::size_t pinned_slot = 0;
    for (std::size_t slot = 0; slot < kSlots; ++slot) {
        const auto handle = static_cast<runtime::RenderSurfaceHandle>(0xC3E000u + slot);
        auto desc = test_desc();
        authority.bind(handle, slot, desc);
        authority.surface_bindings.erase(handle);
        pinned_slot = slot;
    }
    CHECK(authority.physical_count() == kSlots);

    // Rebind the last slot, lease it, and drop the binding: the physical image
    // is now unbound but pinned — exactly the state a recording op leaves
    // behind between the lookup and the ring registration.
    constexpr runtime::RenderSurfaceHandle leased_handle{0xC3E777u};
    auto leased_desc = test_desc();
    authority.bind(leased_handle, pinned_slot, leased_desc);
    const auto lease = authority.acquire_lease(leased_handle);
    CHECK(lease.valid);
    CHECK(lease.image != nullptr);
    CHECK(authority.slot_pinned(pinned_slot));
    CHECK(authority.revalidate(lease));
    authority.surface_bindings.erase(leased_handle);

    const auto destroys_before = owner.destroy_count;
    authority.prune_unused_slots();

    // 70 unused transient slots - 1 pinned = 69 candidates, 8 retained.
    CHECK(owner.destroy_count - destroys_before == 61);
    CHECK(authority.physical_surfaces.contains(pinned_slot));

    authority.release_lease(lease);
    CHECK_FALSE(authority.slot_pinned(pinned_slot));

    // With the lease dropped, the same prune can now reclaim it.
    authority.prune_unused_slots();
    CHECK(owner.destroy_count > destroys_before);
}

TEST_CASE("a leased slot cannot be re-materialized") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    constexpr runtime::RenderSurfaceHandle handle{0xC3E801u};
    auto desc = test_desc(32, 24);
    const auto& image = authority.bind(handle, 3, desc);
    const auto lease = authority.acquire_lease(handle);
    CHECK(lease.image == &image);

    // A resize while a recording holds the lease is an API conflict, not a
    // silent image swap: the writer must fail loudly.
    auto resized = test_desc(64, 48);
    CHECK_THROWS_AS(authority.ensure(handle, resized), std::logic_error);
    CHECK(lease.image->width == 32);

    authority.release_lease(lease);
    // Unleased again, the resize is legal and re-materializes in place.
    auto& rematerialized = authority.ensure(handle, resized);
    CHECK(rematerialized.width == 64);
}

TEST_CASE("the generation token invalidates a stale lease") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    constexpr runtime::RenderSurfaceHandle handle{0xC3E901u};
    auto desc = test_desc(32, 24);
    authority.bind(handle, 5, desc);

    const auto lease = authority.acquire_lease(handle);
    CHECK(authority.revalidate(lease));
    authority.release_lease(lease);

    // Re-materialization bumps the slot token: the token captured by the
    // earlier lease is now stale, so revalidation rejects it.
    auto resized = test_desc(64, 48);
    (void)authority.ensure(handle, resized);
    CHECK_FALSE(authority.revalidate(lease));

    const auto fresh = authority.acquire_lease(handle);
    CHECK(authority.revalidate(fresh));
    CHECK(fresh.token != lease.token);
    authority.release_lease(fresh);
}

// ── Per-frame-batch-slot transient retirement ─────────────────────────────
//
// The retirement is deliberately scoped to handles materialized *inside an
// active frame batch slot*. A host-provided surface created outside a batch is
// never recorded, so a frame boundary must not clear it: the earlier "retire
// every transient in the slot namespace" remedy did exactly that (host content
// kept unbound, `download_surface` failed) and was reverted. These cases pin
// the scoped contract so the reverted behaviour cannot come back silently.

TEST_CASE("a recorded frame-slot transient is retired while a host surface survives") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    // Host-provided surface: bound outside any frame batch, so never recorded.
    constexpr runtime::RenderSurfaceHandle host_handle{0xC3F001u};
    constexpr std::size_t host_slot{100};
    authority.bind(host_handle, host_slot, test_desc());
    authority.physical_surfaces.at(host_slot).image.initialized = true;

    // Frame-transient surface materialized while frame slot 2 was recording.
    constexpr runtime::RenderSurfaceHandle transient_handle{0xC3F002u};
    constexpr std::size_t transient_slot{7};
    constexpr std::size_t frame_slot{2};
    authority.bind(transient_handle, transient_slot, test_desc());
    authority.physical_surfaces.at(transient_slot).image.initialized = true;
    authority.record_frame_slot_transient(frame_slot, transient_handle);

    CHECK(authority.binding_count() == 2);

    CHECK(authority.retire_frame_slot_transients(frame_slot) == 1);

    // The recorded transient lost its binding and its stale content flag, so
    // the next occupant re-materializes instead of trusting old bytes.
    CHECK(authority.surface_bindings.count(transient_handle) == 0);
    CHECK_FALSE(authority.physical_surfaces.at(transient_slot).image.initialized);
    // The host surface was never recorded for this slot: still bound and still
    // initialized. This is the regression the reverted remedy broke.
    CHECK(authority.surface_bindings.count(host_handle) == 1);
    CHECK(authority.physical_surfaces.at(host_slot).image.initialized);

    // Retirement is idempotent: a completed slot with no records releases
    // nothing.
    CHECK(authority.retire_frame_slot_transients(frame_slot) == 0);
}

TEST_CASE("retiring one frame slot leaves another slot's transients bound") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    constexpr runtime::RenderSurfaceHandle slot0_handle{0xC3F011u};
    constexpr runtime::RenderSurfaceHandle slot1_handle{0xC3F012u};
    authority.bind(slot0_handle, 3, test_desc());
    authority.bind(slot1_handle, 4, test_desc());
    authority.record_frame_slot_transient(0, slot0_handle);
    authority.record_frame_slot_transient(1, slot1_handle);

    CHECK(authority.retire_frame_slot_transients(0) == 1);
    CHECK(authority.surface_bindings.count(slot0_handle) == 0);
    CHECK(authority.surface_bindings.count(slot1_handle) == 1);

    CHECK(authority.retire_frame_slot_transients(1) == 1);
    CHECK(authority.surface_bindings.count(slot1_handle) == 0);
}

TEST_CASE("released pipeline slots are reclaimed while job-persistent images survive") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    auto pipeline_desc = test_desc();
    pipeline_desc.lifetime = runtime::LifetimeClass::PipelineSlot;
    auto persistent_desc = test_desc();
    persistent_desc.lifetime = runtime::LifetimeClass::JobPersistent;

    constexpr std::size_t kReleasedPipelineSlots = 12;
    for (std::size_t slot = 0; slot < kReleasedPipelineSlots; ++slot) {
        const auto handle = static_cast<runtime::RenderSurfaceHandle>(0xC3F200u + slot);
        authority.bind(handle, slot, pipeline_desc);
        authority.surface_bindings.erase(handle); // owner released its session ring slot
    }
    constexpr runtime::RenderSurfaceHandle cached_asset{0xC3F300u};
    authority.bind(cached_asset, 100, persistent_desc);

    authority.prune_unused_slots(
        VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner>::kMaxRetainedTransientSlots);

    CHECK(authority.physical_count() ==
          VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner>::kMaxRetainedTransientSlots + 1);
    CHECK(authority.surface_bindings.contains(cached_asset));
    CHECK(authority.physical_surfaces.contains(100));
    CHECK(owner.destroy_count == kReleasedPipelineSlots -
          VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner>::kMaxRetainedTransientSlots);
}

TEST_CASE("retirement keeps the transient warm pool bounded") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    constexpr std::size_t kSlots = 12;
    constexpr std::size_t frame_slot = 5;
    for (std::size_t slot = 0; slot < kSlots; ++slot) {
        const auto handle =
            static_cast<runtime::RenderSurfaceHandle>(0xC3F100u + slot);
        authority.bind(handle, slot, test_desc());
        authority.record_frame_slot_transient(frame_slot, handle);
    }
    CHECK(authority.physical_count() == kSlots);

    const auto destroys_before = owner.destroy_count;
    CHECK(authority.retire_frame_slot_transients(frame_slot) == kSlots);
    CHECK(authority.binding_count() == 0);

    // All retired slots become unused transients; prune keeps only the warm
    // pool bound (8 retained, ~265MB at 1080p RGBA32F) instead of letting a
    // per-frame 33MB image accumulate to VRAM exhaustion.
    CHECK(authority.physical_count() ==
          VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner>::kMaxRetainedTransientSlots);
    CHECK(owner.destroy_count - destroys_before ==
          kSlots - VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner>::kMaxRetainedTransientSlots);
}

TEST_CASE("nested leases on one slot are reference counted") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;

    constexpr runtime::RenderSurfaceHandle handle{0xC3EA01u};
    auto desc = test_desc();
    authority.bind(handle, 2, desc);

    // A plan-wide lease plus a per-op lease of the same surface must both be
    // held and both released (that is how the batch set and the pass set
    // coexist); the slot stays pinned until the last one is dropped.
    const auto plan_lease = authority.acquire_lease(handle);
    const auto op_lease = authority.acquire_lease(handle);
    CHECK(plan_lease.image == op_lease.image);
    CHECK(authority.slot_pinned(2));

    authority.release_lease(op_lease);
    CHECK(authority.slot_pinned(2));
    authority.release_lease(plan_lease);
    CHECK_FALSE(authority.slot_pinned(2));
}

TEST_CASE("recording slots are carried as an exact per-handle mask") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    constexpr runtime::RenderSurfaceHandle handle{0xD0E001u};

    CHECK(authority.recording_slot_mask(handle) == 0);
    authority.note_recording_slot(handle, 2);
    authority.note_recording_slot(handle, 4);
    authority.note_recording_slot(handle, 2); // idempotent
    CHECK(authority.recording_slot_mask(handle) ==
          (frame_slot_bit(2) | frame_slot_bit(4)));

    // Out-of-ring slots and the invalid handle are never recorded.
    authority.note_recording_slot(handle, VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner>::kFrameBatchSlotCount);
    authority.note_recording_slot(runtime::kInvalidRenderSurfaceHandle, 1);
    CHECK(authority.recording_slot_mask(handle) ==
          (frame_slot_bit(2) | frame_slot_bit(4)));
    CHECK(authority.recording_slot_mask(runtime::kInvalidRenderSurfaceHandle) == 0);

    // A release carries the mask it was queued with, not a single guessed slot.
    const auto mask = authority.recording_slot_mask(handle);
    authority.enqueue_deferred_release({handle, 4, 9, mask});
    REQUIRE(authority.deferred_surface_releases.size() == 1);
    CHECK(authority.deferred_surface_releases.front().slot_mask == mask);
    CHECK(authority.deferred_surface_releases.front().slot == 4);

    authority.forget_recording_slots(handle);
    CHECK(authority.recording_slot_mask(handle) == 0);
}

TEST_CASE("a release waits for every recorded slot, never a single guess") {
    std::array<bool, 6> busy{};
    const auto mask = static_cast<std::uint8_t>(frame_slot_bit(2) | frame_slot_bit(4));

    // Idle ring: retired.
    CHECK(recording_slots_retired(mask, std::span<const bool>(busy)));

    // The recorded slot 4 is in flight while the guessed/latest slot 2 is idle:
    // the release must wait.
    busy[4] = true;
    CHECK_FALSE(recording_slots_retired(mask, std::span<const bool>(busy)));

    // A busy slot outside the mask does not gate this release.
    busy = {};
    busy[0] = true;
    CHECK(recording_slots_retired(mask, std::span<const bool>(busy)));

    // Never-recorded handles (mask 0) are not gated by the ring at all.
    busy = {};
    busy[3] = true;
    CHECK(recording_slots_retired(0, std::span<const bool>(busy)));
}

TEST_CASE("a surface referenced by an in-flight slot is never freed") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    FakeSurfaceOwner owner;
    authority.owner_ = &owner;
    constexpr runtime::RenderSurfaceHandle handle{0xD0E002u};
    const auto desc = test_desc();
    authority.bind(handle, 7, desc);

    // The handle was referenced by slot 1 (retired) and slot 3 (in flight).
    authority.record_frame_slot_transient(1, handle);
    authority.record_frame_slot_transient(3, handle);
    authority.note_recording_slot(handle, 1);
    authority.note_recording_slot(handle, 3);

    std::array<bool, 6> busy{};
    busy[3] = true;
    CHECK(authority.retire_frame_slot_transients(1, std::span<const bool>(busy)) == 0);
    CHECK(authority.bound_slot(handle) == 7);
    CHECK(authority.binding_count() == 1);
    CHECK(owner.destroy_count == 0);
    CHECK(authority.recording_slot_mask(handle) == frame_slot_bit(3));

    // Slot 3 completes: now the last reference is gone and the binding releases.
    busy = {};
    CHECK(authority.retire_frame_slot_transients(3, std::span<const bool>(busy)) == 1);
    CHECK(authority.binding_count() == 0);
    CHECK(authority.recording_slot_mask(handle) == 0);
    CHECK(owner.destroy_count == 0); // warm pool retains the image
}

TEST_CASE("deferred releases keep their masks through the overflow path") {
    VulkanSurfaceAuthority<FakeImage, FakeSurfaceOwner> authority;
    constexpr runtime::RenderSurfaceHandle first{0xD0E003u};
    constexpr runtime::RenderSurfaceHandle second{0xD0E004u};
    constexpr runtime::RenderSurfaceHandle third{0xD0E005u};

    authority.enqueue_deferred_release({first, 0, 1, frame_slot_bit(0)});
    authority.enqueue_deferred_release({second, 2, 1, frame_slot_bit(2)});
    CHECK_FALSE(authority.note_overflow_if_needed(2));
    authority.enqueue_deferred_release({third, 5, 1, frame_slot_bit(5) | frame_slot_bit(1)});

    CHECK(authority.note_overflow_if_needed(2));
    CHECK(authority.overflow_wait_count == 1);
    REQUIRE(authority.deferred_surface_releases.size() == 3);
    CHECK(authority.deferred_surface_releases[2].slot_mask ==
          (frame_slot_bit(5) | frame_slot_bit(1)));
    CHECK(authority.pending_release_high_water == 3);
}

} // TEST_SUITE

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
