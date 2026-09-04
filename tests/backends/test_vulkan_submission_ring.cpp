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
        const chronon3d::runtime::SurfaceDesc desc{
            .width = 32,
            .height = 24,
            .format = chronon3d::runtime::PixelFormat::Rgba32Float,
            .usage = chronon3d::runtime::ResourceUsage::Generic,
            .lifetime = chronon3d::runtime::LifetimeClass::FrameTransient};
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
    // SubmissionRing has depth 3. The fourth frame must wrap to slot 0 and
    // retire/reuse that slot's fence before recording into it again.
    for (int frame = 0; frame < 4; ++frame) {
        fixture.backend->begin_frame_batch();
        CHECK(fixture.backend->fill_rect_surface(
                  fixture.handle, 0, 0, 4, 4,
                  chronon3d::Color{0.4f, 0.2f, 0.1f, 1.0f})
                  .ok());
        fixture.backend->end_frame_batch();
    }
    const auto after = fixture.backend->stats();
    CHECK(after.submissions == before.submissions + 4);
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