#include <doctest/doctest.h>

#ifdef CHRONON3D_ENABLE_VULKAN

#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/render_graph/nodes/composite_node.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/runtime/gpu_layer_batch.hpp>

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

TEST_CASE("readback preserves transient batch output and standalone fence ownership") {
    BackendFixture fixture;
    REQUIRE(fixture.backend);

    std::vector<float> pixels(32u * 24u * 4u, 0.0f);
    // Exercise slot wraparound and alternating batch/readback submissions.
    // Waiting must not retire the transient output before its consumer reads it,
    // and a batch signal must never masquerade as a standalone fence submission.
    for (int frame = 0; frame < 14; ++frame) {
        const float red = static_cast<float>(frame + 1) / 16.0f;
        fixture.backend->begin_frame_batch();
        REQUIRE(fixture.backend->fill_rect_surface(
            fixture.handle, 0, 0, 32, 24,
            chronon3d::Color{red, 0.25f, 0.5f, 1.0f}).ok());
        fixture.backend->end_frame_batch();
        REQUIRE(fixture.backend->download_surface(fixture.handle, pixels).ok());
        CHECK(fixture.backend->is_native_surface_valid(fixture.handle));
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            CHECK(pixels[i] == doctest::Approx(red));
            CHECK(pixels[i + 1] == doctest::Approx(0.25f));
            CHECK(pixels[i + 2] == doctest::Approx(0.5f));
            CHECK(pixels[i + 3] == doctest::Approx(1.0f));
        }
    }
}

TEST_CASE("Vulkan fused layer groups synchronize destination writes between dispatches") {
    using namespace chronon3d;
    BackendFixture fixture;
    REQUIRE(fixture.backend);

    const auto desc = runtime::SurfaceDesc::make(
        32, 24, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage, runtime::LifetimeClass::JobPersistent);
    constexpr std::array<runtime::RenderSurfaceHandle, 3> source_handles{
        0xC3D201u, 0xC3D202u, 0xC3D203u};
    const std::array<std::array<float, 4>, 3> source_colors{{
        {0.0f, 0.0f, 1.0f, 1.0f},       // opaque blue base
        {0.5f, 0.0f, 0.0f, 0.5f},       // premultiplied half-alpha red
        {0.0f, 0.5f, 0.0f, 0.5f},       // premultiplied half-alpha green
    }};
    std::vector<runtime::LayerInstance> instances(3);
    for (std::size_t i = 0; i < source_handles.size(); ++i) {
        REQUIRE(fixture.backend->create_surface(source_handles[i], desc).ok());
        std::vector<float> pixels(32u * 24u * 4u);
        for (std::size_t pixel = 0; pixel < pixels.size() / 4; ++pixel) {
            std::copy(source_colors[i].begin(), source_colors[i].end(),
                      pixels.begin() + static_cast<std::ptrdiff_t>(pixel * 4));
        }
        REQUIRE(fixture.backend->upload_surface(
            source_handles[i], desc, pixels).ok());
        instances[i].kind = runtime::PrimitiveKind::Image;
        instances[i].resource_index = static_cast<std::uint32_t>(i);
        instances[i].dst_x0 = 0.0f;
        instances[i].dst_y0 = 0.0f;
        instances[i].dst_x1 = 32.0f;
        instances[i].dst_y1 = 24.0f;
        instances[i].blend = BlendMode::Normal;
    }

    fixture.backend->begin_frame_batch();
    REQUIRE(fixture.backend->initialize_transparent_surface(fixture.handle).ok());
    REQUIRE(fixture.backend->execute_layer_batch(
        fixture.handle, instances, source_handles, {}, {}).ok());
    fixture.backend->end_frame_batch();

    std::vector<float> pixels(32u * 24u * 4u);
    REQUIRE(fixture.backend->download_surface(fixture.handle, pixels).ok());
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        CHECK(pixels[i] == doctest::Approx(0.25f));
        CHECK(pixels[i + 1] == doctest::Approx(0.5f));
        CHECK(pixels[i + 2] == doctest::Approx(0.25f));
        CHECK(pixels[i + 3] == doctest::Approx(1.0f));
    }
    for (const auto handle : source_handles) {
        REQUIRE(fixture.backend->release_surface(handle).ok());
    }
}

TEST_CASE("Composite zero-copy transfer preserves GPU layers instead of reuploading CPU mirror") {
    using namespace chronon3d;
    BackendFixture fixture;
    REQUIRE(fixture.backend);
    runtime::RenderSurfaceRegistry registry;
    const auto desc = runtime::SurfaceDesc::make(
        32, 24, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage, runtime::LifetimeClass::FrameTransient);
    const auto handle = registry.create(desc);
    REQUIRE(fixture.backend->create_surface(handle, desc).ok());

    graph::RenderGraphContext ctx;
    ctx.backend = fixture.backend.get();
    ctx.surface_registry = &registry;
    ctx.frame_input.width = 32;
    ctx.frame_input.height = 24;
    ctx.framebuffer_pool = cache::FramebufferPool::create_shared(1024 * 1024);
    auto bottom = ctx.framebuffer_pool->acquire_shared(32, 24, true);
    bottom->clear(Color{0.0f, 0.0f, 1.0f, 1.0f}); // Deliberately stale CPU mirror.
    bottom->set_surface_handle(handle);
    bottom->mark_gpu_authoritative();
    ctx.node_exec.reusable_bottom = std::move(bottom);
    REQUIRE(ctx.node_exec.reusable_bottom.use_count() == 1);

    fixture.backend->begin_frame_batch();
    REQUIRE(fixture.backend->fill_rect_surface(
        handle, 0, 0, 32, 24, Color{1.0f, 0.0f, 0.0f, 1.0f}).ok());
    graph::CompositeNode node{1, BlendMode::Normal};
    const std::array<FramebufferRef, 1> inputs{ctx.node_exec.reusable_bottom.get()};
    const auto before = fixture.backend->stats();
    auto executed = node.execute(ctx, inputs, {});
    REQUIRE(executed);
    auto output = executed.take_value();
    REQUIRE(output);
    CHECK(output->surface_handle() == handle);
    CHECK(output->is_gpu_authoritative());
    CHECK(fixture.backend->stats().upload_calls == before.upload_calls);
    fixture.backend->end_frame_batch();

    std::vector<float> pixels(32u * 24u * 4u);
    REQUIRE(fixture.backend->download_surface(output->surface_handle(), pixels).ok());
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        CHECK(pixels[i] == doctest::Approx(1.0f));
        CHECK(pixels[i + 1] == doctest::Approx(0.0f));
        CHECK(pixels[i + 2] == doctest::Approx(0.0f));
        CHECK(pixels[i + 3] == doctest::Approx(1.0f));
    }
    REQUIRE(fixture.backend->release_surface(handle).ok());
}

TEST_CASE("Affine CPU source promotion cannot consume the planned output slot") {
    using namespace chronon3d;
    BackendFixture fixture;
    REQUIRE(fixture.backend);
    runtime::RenderSurfaceRegistry registry;
    const auto desc = runtime::SurfaceDesc::make(
        32, 24, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage, runtime::LifetimeClass::FrameTransient);
    const auto handle = registry.create(desc);
    REQUIRE(fixture.backend->create_surface(handle, desc).ok());
    graph::RenderGraphContext ctx;
    ctx.backend = fixture.backend.get();
    ctx.surface_registry = &registry;
    ctx.frame_input.width = 32;
    ctx.frame_input.height = 24;
    ctx.framebuffer_pool = cache::FramebufferPool::create_shared(1024 * 1024);
    auto bottom = ctx.framebuffer_pool->acquire_shared(32, 24, true);
    bottom->set_surface_handle(handle);
    bottom->mark_gpu_authoritative();
    ctx.node_exec.reusable_bottom = std::move(bottom);
    // An output slot retaining the destination handle must not become an
    // input promotion buffer when zero-copy acquisition leaves it unused.
    auto slot = ctx.framebuffer_pool->acquire_owned(32, 24, true);
    slot->set_surface_handle(handle);
    slot->mark_gpu_authoritative();
    ctx.node_exec.planned_physical_slot = &slot;
    Framebuffer top(4, 4);
    top.clear(Color{0.0f, 1.0f, 0.0f, 1.0f});
    top.set_origin(2, 3);
    const std::array<FramebufferRef, 2> inputs{ctx.node_exec.reusable_bottom.get(), &top};
    fixture.backend->begin_frame_batch();
    REQUIRE(fixture.backend->fill_rect_surface(
        handle, 0, 0, 32, 24, Color{1.0f, 0.0f, 0.0f, 1.0f}).ok());
    graph::CompositeNode node{2, BlendMode::Normal};
    auto executed = node.execute(ctx, inputs, {});
    REQUIRE(executed);
    auto output = executed.take_value();
    REQUIRE(output);
    CHECK(output->surface_handle() == handle);
    CHECK(registry.lookup(handle).has_value());
    CHECK(slot->width() == 32);
    CHECK(slot->height() == 24);
    fixture.backend->end_frame_batch();
    std::vector<float> pixels(32u * 24u * 4u);
    REQUIRE(fixture.backend->download_surface(handle, pixels).ok());
    for (int y = 0; y < 24; ++y) for (int x = 0; x < 32; ++x) {
        const auto i = static_cast<std::size_t>(y * 32 + x) * 4;
        const bool green = x >= 2 && x < 6 && y >= 3 && y < 7;
        CHECK(pixels[i] == doctest::Approx(green ? 0.0f : 1.0f));
        CHECK(pixels[i + 1] == doctest::Approx(green ? 1.0f : 0.0f));
        CHECK(pixels[i + 2] == doctest::Approx(0.0f));
        CHECK(pixels[i + 3] == doctest::Approx(1.0f));
    }
    REQUIRE(fixture.backend->release_surface(handle).ok());
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