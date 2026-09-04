#include <doctest/doctest.h>

#ifdef CHRONON3D_ENABLE_VULKAN

#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#include <array>
#include <memory>
#include <vector>

namespace {

struct UploadBackendFixture {
    std::unique_ptr<chronon3d::backends::vulkan::VulkanBackend> backend;
    chronon3d::runtime::RenderSurfaceHandle handle{0xC3D201u};
    chronon3d::runtime::SurfaceDesc desc{
        .width = 32,
        .height = 24,
        .format = chronon3d::runtime::PixelFormat::Rgba32Float,
        .usage = chronon3d::runtime::ResourceUsage::Generic,
        .lifetime = chronon3d::runtime::LifetimeClass::JobPersistent};
    std::vector<float> pixels;

    UploadBackendFixture()
        : pixels(static_cast<std::size_t>(desc.width) * desc.height * 4u, 0.25f) {
        auto base = chronon3d::backends::vulkan::make_vulkan_backend(0);
        backend.reset(dynamic_cast<chronon3d::backends::vulkan::VulkanBackend*>(base.release()));
        if (!backend) return;
        if (!backend->create_surface(handle, desc).ok()) backend.reset();
    }

    ~UploadBackendFixture() {
        if (backend) backend->release_surface(handle);
    }
};

} // namespace

TEST_SUITE("VulkanUploadRing") {

TEST_CASE("async upload ring reuses warmed storage after wraparound") {
    UploadBackendFixture fixture;
    if (!fixture.backend) { WARN("No Vulkan device available"); return; }

    const auto before = fixture.backend->stats();
    std::array<chronon3d::runtime::UploadTicket, 3> warmup{};

    // Fill every ring slot once. These are the only same-capacity backing
    // allocations the upload ring is allowed to need.
    for (auto& ticket : warmup) {
        CHECK(fixture.backend->upload_surface_async(
                  fixture.handle, fixture.desc, fixture.pixels, ticket)
                  .ok());
        CHECK(ticket.valid());
    }
    for (const auto& ticket : warmup) {
        CHECK(fixture.backend->wait_upload(ticket).ok());
    }

    const auto warmed = fixture.backend->stats();
    CHECK(warmed.staging_allocations >= before.staging_allocations + 1);
    CHECK(warmed.staging_allocations <= before.staging_allocations + 3);

    // Cross the 3-slot boundary twice. With unchanged capacity this must only
    // rotate/reuse slots; it must not allocate new per-frame staging buffers.
    for (int upload = 0; upload < 6; ++upload) {
        chronon3d::runtime::UploadTicket ticket{};
        CHECK(fixture.backend->upload_surface_async(
                  fixture.handle, fixture.desc, fixture.pixels, ticket)
                  .ok());
        CHECK(ticket.valid());
        CHECK(fixture.backend->wait_upload(ticket).ok());
    }

    const auto after = fixture.backend->stats();
    CHECK(after.staging_allocations == warmed.staging_allocations);
    CHECK(after.upload_calls == warmed.upload_calls + 6);
}

} // TEST_SUITE

#endif // CHRONON3D_ENABLE_VULKAN
