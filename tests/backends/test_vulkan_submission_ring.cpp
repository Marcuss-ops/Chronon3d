#include <doctest/doctest.h>

#ifdef CHRONON3D_ENABLE_VULKAN

#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/runtime/render_surface.hpp>

namespace {

struct BackendFixture {
    std::unique_ptr<chronon3d::backends::vulkan::VulkanBackend> backend;
    chronon3d::runtime::RenderSurfaceHandle handle{0xC3D101u};

    BackendFixture() {
        auto base = chronon3d::backends::vulkan::make_vulkan_backend(0);
        backend.reset(dynamic_cast<chronon3d::backends::vulkan::VulkanBackend*>(base.release()));
        if (!backend) return;
        const chronon3d::runtime::SurfaceDesc desc = chronon3d::runtime::SurfaceDesc::make(
            32, 24, chronon3d::runtime::PixelFormat::Rgba32Float,
            chronon3d::runtime::ResourceUsage::Generic,
            chronon3d::runtime::LifetimeClass::FrameTransient,
            chronon3d::runtime::ColorMetadata{});
        if (!backend->create_surface(handle, desc).ok()) backend.reset();
    }

    ~BackendFixture() {
        if (backend) backend->release_surface(handle);
    }
};

} // namespace

TEST_SUITE("VulkanSubmissionRing") {

TEST_CASE("rotates frame slots without changing one submission per frame") {
    BackendFixture fixture;
    if (!fixture.backend) { WARN("No Vulkan device available"); return; }

    const auto before = fixture.backend->stats();
    for (int frame = 0; frame < 3; ++frame) {
        fixture.backend->begin_frame_batch();
        CHECK(fixture.backend->fill_rect_surface(
                  fixture.handle, 0, 0, 8, 8,
                  chronon3d::Color{0.1f, 0.2f, 0.3f, 1.0f})
                  .ok());
        fixture.backend->end_frame_batch();
    }
    const auto after = fixture.backend->stats();
    CHECK(after.submissions == before.submissions + 3);
    CHECK(after.passes_executed >= before.passes_executed + 3);
}

TEST_CASE("wraparound reuses the first frame slot fence") {
    BackendFixture fixture;
    if (!fixture.backend) { WARN("No Vulkan device available"); return; }

    const auto before = fixture.backend->stats();
    // SubmissionRing has depth 6. The seventh frame must wrap to slot 0 and
    // retire/reuse that slot's fence before recording into it again.
    for (int frame = 0; frame < 7; ++frame) {
        fixture.backend->begin_frame_batch();
        CHECK(fixture.backend->fill_rect_surface(
                  fixture.handle, 0, 0, 4, 4,
                  chronon3d::Color{0.4f, 0.2f, 0.1f, 1.0f})
                  .ok());
        fixture.backend->end_frame_batch();
    }
    const auto after = fixture.backend->stats();
    CHECK(after.submissions == before.submissions + 7);
    CHECK(after.frame_slot_wait_count >= before.frame_slot_wait_count + 1);
}

TEST_CASE("empty command batch is abandoned without a submission") {
    BackendFixture fixture;
    if (!fixture.backend) { WARN("No Vulkan device available"); return; }

    const auto before = fixture.backend->stats();
    fixture.backend->begin_command_batch();
    fixture.backend->end_command_batch();
    const auto after = fixture.backend->stats();

    CHECK(after.submissions == before.submissions);
    CHECK(after.passes_executed == before.passes_executed);
    CHECK_FALSE(after.command_batch_active);
    CHECK_FALSE(after.command_batch_started);
}

TEST_CASE("one command batch submits once for multiple frames") {
    BackendFixture fixture;
    if (!fixture.backend) { WARN("No Vulkan device available"); return; }

    const auto before = fixture.backend->stats();
    fixture.backend->begin_command_batch();
    for (int frame = 0; frame < 3; ++frame) {
        fixture.backend->begin_frame_batch();
        CHECK(fixture.backend->fill_rect_surface(
                  fixture.handle, frame, 0, 4, 4,
                  chronon3d::Color{0.2f, 0.3f, 0.4f, 1.0f})
                  .ok());
        fixture.backend->end_frame_batch();
    }
    CHECK(fixture.backend->stats().submissions == before.submissions);
    fixture.backend->end_command_batch();
    const auto after = fixture.backend->stats();
    CHECK(after.submissions == before.submissions + 1);
    CHECK(after.passes_executed >= before.passes_executed + 3);
}

TEST_CASE("recording ops take the contention domains separately (audit N4)") {
    BackendFixture fixture;
    if (!fixture.backend) { WARN("No Vulkan device available"); return; }

    // Open the recording window first: a batch transition is itself a dual
    // site, so it must not be inside the measured interval.
    fixture.backend->begin_frame_batch();
    const auto before_op = fixture.backend->stats();

    CHECK(fixture.backend->fill_rect_surface(
              fixture.handle, 0, 0, 4, 4,
              chronon3d::Color{0.5f, 0.5f, 0.5f, 1.0f})
              .ok());

    const auto after_op = fixture.backend->stats();
    // The recording op waits on the submission ring...
    CHECK(after_op.submission_lock_acquires > before_op.submission_lock_acquires);
    // ...leases its surface through the store lock...
    CHECK(after_op.surface_lock_acquires > before_op.surface_lock_acquires);
    // ...and never holds the two domains together. This equality is the whole
    // point of the decoupling, so it is asserted as a regression alarm rather
    // than read as a statistic.
    CHECK(after_op.dual_lock_acquires == before_op.dual_lock_acquires);

    fixture.backend->end_frame_batch();

    // The remaining coupling is measured, not assumed: the batch transitions
    // still need both domains, and they report it.
    const auto after_batch = fixture.backend->stats();
    CHECK(after_batch.dual_lock_acquires > after_op.dual_lock_acquires);

    // A readback resolves a surface and submits in the same call, so it is a
    // dual site by need — the counter says so.
    std::vector<float> pixels(32u * 24u * 4u, 0.0f);
    const auto before_download = fixture.backend->stats();
    REQUIRE(fixture.backend->download_surface(fixture.handle, pixels).ok());
    const auto after_download = fixture.backend->stats();
    CHECK(after_download.dual_lock_acquires > before_download.dual_lock_acquires);
}

TEST_CASE("multiple command batches remain independently submitted") {
    BackendFixture fixture;
    if (!fixture.backend) { WARN("No Vulkan device available"); return; }

    const auto before = fixture.backend->stats();
    for (int batch = 0; batch < 2; ++batch) {
        fixture.backend->begin_command_batch();
        fixture.backend->begin_frame_batch();
        CHECK(fixture.backend->fill_rect_surface(
                  fixture.handle, 0, 0, 4, 4,
                  chronon3d::Color{0.6f, 0.1f, 0.2f, 1.0f})
                  .ok());
        fixture.backend->end_frame_batch();
        fixture.backend->end_command_batch();
    }
    const auto after = fixture.backend->stats();
    CHECK(after.submissions == before.submissions + 2);
}

} // TEST_SUITE
#endif