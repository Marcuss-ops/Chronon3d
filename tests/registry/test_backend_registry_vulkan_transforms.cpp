// test_backend_registry_vulkan_transforms.cpp — Split from
// test_backend_registry_vulkan.cpp: upload staging, async upload tickets,
// native transform / affine / blur / glow and frame-batch submission
// coverage (Vulkan-gated as in the origin).
#include <doctest/doctest.h>

#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <chronon3d/render_graph/backend_registry.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/nodes/track_matte_node.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/resource_plan.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/render_graph/checkbackend.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/backends/software/effects/per_pixel_dof.hpp>
#include <tests/helpers/cpu_gpu_parity.hpp>
#include <tests/helpers/gpu_readiness_gate.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <blend2d.h>
#endif

#include "../../src/render_graph/nodes/text_run/gpu_text_run.hpp"

#ifdef CHRONON3D_ENABLE_VULKAN
TEST_CASE("Vulkan upload staging is reused after warmup and telemetry is observable") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, 0};
    const std::vector<float> pixels(16, 0.25f);
    REQUIRE(backend.create_surface(111, desc).ok());
    REQUIRE(backend.upload_surface(111, desc, pixels).ok());
    const auto warmed = backend.stats();
    REQUIRE(backend.upload_surface(111, desc, pixels).ok());
    const auto reused = backend.stats();
    CHECK(warmed.staging_allocations == 1);
    CHECK(reused.staging_allocations == warmed.staging_allocations);
    CHECK(reused.upload_calls == warmed.upload_calls + 1);
    CHECK(reused.upload_bytes == warmed.upload_bytes + pixels.size() * sizeof(float));
    CHECK(reused.submissions == warmed.submissions + 1);
}

TEST_CASE("Vulkan async upload returns a timeline ticket and waits before readback") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, 0};
    const std::vector<float> pixels(16, 0.75f);
    std::vector<float> output(16, 0.0f);
    REQUIRE(backend.create_surface(121, desc).ok());
    chronon3d::runtime::UploadTicket ticket{};
    REQUIRE(backend.upload_surface_async(121, desc, pixels, ticket).ok());
    CHECK(ticket.valid());
    REQUIRE(backend.wait_upload(ticket).ok());
    REQUIRE(backend.download_surface(121, output).ok());
    CHECK(output[0] == doctest::Approx(0.75f));
    CHECK(backend.stats().submissions >= 2);
}

TEST_CASE("Vulkan frame batch waits on asynchronous uploads before compute") {
    using namespace chronon3d;
    backends::vulkan::VulkanBackend backend;
    const runtime::SurfaceDesc source_desc{
        2, 2, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::JobPersistent, 0};
    const runtime::SurfaceDesc destination_desc{
        4, 4, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::JobPersistent, 0};
    const std::vector<float> pixels(16, 1.0f);
    std::vector<float> output(64, 0.0f);

    REQUIRE(backend.create_surface(126, source_desc).ok());
    REQUIRE(backend.create_surface(127, destination_desc).ok());
    for (int iteration = 0; iteration < 8; ++iteration) {
        backend.begin_frame_batch();
        runtime::UploadTicket ticket{};
        REQUIRE(backend.upload_surface_async(126, source_desc, pixels, ticket).ok());
        REQUIRE(ticket.valid());
        // Intentionally do not host-wait on the upload ticket. The frame
        // submission must wait on the transfer timeline before sampling.
        REQUIRE(backend.transform_surface(127, 126, 1, 1, 0.5f).ok());
        backend.end_frame_batch();
        REQUIRE(backend.download_surface(127, output).ok());
        CHECK(output[(1 * 4 + 1) * 4] == doctest::Approx(0.5f));
        CHECK(output[(0 * 4 + 0) * 4] == doctest::Approx(0.0f));
    }
}

TEST_CASE("Vulkan surface release waits for asynchronous work") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, 0};
    const std::vector<float> pixels(16, 0.35f);
    chronon3d::runtime::UploadTicket ticket{};
    REQUIRE(backend.create_surface(122, desc).ok());
    REQUIRE(backend.upload_surface_async(122, desc, pixels, ticket).ok());
    REQUIRE(backend.release_surface(122).ok());
    CHECK(backend.stats().surface_releases == 1);
}

TEST_CASE("Vulkan surface reuse survives 100 async upload/readback/release cycles") {
    using namespace chronon3d;
    using namespace chronon3d::runtime;

    backends::vulkan::VulkanBackend backend;
    constexpr RenderSurfaceHandle handle = 129;
    constexpr int cycle_count = 100;
    const SurfaceDesc desc{
        2, 2, PixelFormat::Rgba32Float,
        ResourceUsage::Storage, LifetimeClass::JobPersistent, 0};
    const auto before = backend.stats();

    for (int cycle = 0; cycle < cycle_count; ++cycle) {
        REQUIRE(backend.create_surface(handle, desc).ok());
        const float value = static_cast<float>(cycle + 1) /
                            static_cast<float>(cycle_count + 1);
        const std::vector<float> input{
            value, 0.25f, 0.5f, 1.0f,
            value, 0.25f, 0.5f, 1.0f,
            value, 0.25f, 0.5f, 1.0f,
            value, 0.25f, 0.5f, 1.0f};
        UploadTicket ticket{};
        REQUIRE(backend.upload_surface_async(handle, desc, input, ticket).ok());
        REQUIRE(ticket.valid());
        REQUIRE(backend.wait_upload(ticket).ok());

        std::vector<float> output(input.size(), 0.0f);
        REQUIRE(backend.download_surface(handle, output).ok());
        CHECK(output == input);
        REQUIRE(backend.release_surface(handle).ok());
    }

    const auto after = backend.stats();
    CHECK(after.surface_creations == before.surface_creations + cycle_count);
    CHECK(after.surface_releases == before.surface_releases + cycle_count);
}

TEST_CASE("Vulkan upload ring queues three assets before waiting") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, 0};
    // Locals are named ticket_a/b/c (not f-prefixed) so the
    // check_frame_value_convention gate (targeting Frame::value) does not
    // over-match: these are UploadTicket ordering probes, not Frame reads.
    const std::vector<float> ticket_a_pixels(16, 0.1f);
    const std::vector<float> ticket_b_pixels(16, 0.2f);
    const std::vector<float> ticket_c_pixels(16, 0.3f);
    chronon3d::runtime::UploadTicket ticket_a{};
    chronon3d::runtime::UploadTicket ticket_b{};
    chronon3d::runtime::UploadTicket ticket_c{};

    REQUIRE(backend.create_surface(123, desc).ok());
    REQUIRE(backend.create_surface(124, desc).ok());
    REQUIRE(backend.create_surface(125, desc).ok());
    REQUIRE(backend.upload_surface_async(123, desc, ticket_a_pixels, ticket_a).ok());
    REQUIRE(backend.upload_surface_async(124, desc, ticket_b_pixels, ticket_b).ok());
    REQUIRE(backend.upload_surface_async(125, desc, ticket_c_pixels, ticket_c).ok());
    CHECK(ticket_a.value < ticket_b.value);
    CHECK(ticket_b.value < ticket_c.value);
    CHECK(backend.stats().staging_allocations >= 3);
    REQUIRE(backend.wait_upload(ticket_a).ok());
    REQUIRE(backend.wait_upload(ticket_b).ok());
    REQUIRE(backend.wait_upload(ticket_c).ok());
    std::vector<float> output(16, 0.0f);
    REQUIRE(backend.download_surface(125, output).ok());
    CHECK(output[0] == doctest::Approx(0.3f));
}

TEST_CASE("Vulkan native transform applies integer translation and opacity") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc source_desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const chronon3d::runtime::SurfaceDesc destination_desc{
        4, 4, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> source(16, 1.0f);
    std::vector<float> output(64, -1.0f);
    REQUIRE(backend.create_surface(201, source_desc).ok());
    REQUIRE(backend.create_surface(202, destination_desc).ok());
    REQUIRE(backend.upload_surface(201, source_desc, source).ok());
    REQUIRE(backend.transform_surface(202, 201, 1, 1, 0.5f).ok());
    REQUIRE(backend.download_surface(202, output).ok());
    CHECK(output[(1 * 4 + 1) * 4] == doctest::Approx(0.5f));
    CHECK(output[(2 * 4 + 2) * 4 + 3] == doctest::Approx(0.5f));
    CHECK(output[(0 * 4 + 0) * 4] == doctest::Approx(0.0f));
}

TEST_CASE("Vulkan native transform marks its output GPU-authoritative") {
    using namespace chronon3d;
    using namespace chronon3d::runtime;

    backends::vulkan::VulkanBackend backend;
    RenderSurfaceRegistry registry;
    constexpr std::uint32_t size = 4;
    const auto desc = SurfaceDesc::make(
        size, size, PixelFormat::Rgba32Float,
        ResourceUsage::Storage, LifetimeClass::FrameTransient);
    const auto source_surface = registry.create(desc);
    REQUIRE(source_surface != kInvalidRenderSurfaceHandle);
    REQUIRE(backend.create_surface(source_surface, desc).ok());
    const std::vector<float> pixels(size * size * 4, 0.5f);
    REQUIRE(backend.upload_surface(source_surface, desc, pixels).ok());

    Framebuffer source(size, size);
    source.set_surface_handle(source_surface);
    source.mark_gpu_authoritative();
    graph::RenderGraphContext ctx;
    ctx.frame_input.width = size;
    ctx.frame_input.height = size;
    ctx.policy.require_native_gpu = true;
    ctx.backend = &backend;
    ctx.surface_registry = &registry;

    Mat4 matrix{1.0f};
    matrix[3][0] = 1.0f;
    graph::TransformNode node(matrix, 1.0f, SamplingMode::Nearest);
    const std::array<FramebufferRef, 1> inputs{FramebufferRef{source}};
    auto result = node.execute(ctx, inputs, {});
    REQUIRE(result.ok());
    auto output = result.take_value();
    REQUIRE(output);
    CHECK(output->surface_handle() != kInvalidRenderSurfaceHandle);
    CHECK(output->is_gpu_authoritative());
    CHECK_FALSE(output->is_cpu_authoritative());

    const auto output_surface = output->surface_handle();
    output->clear_surface_handle();
    (void)backend.release_surface(output_surface);
    (void)registry.release(output_surface);
    (void)backend.release_surface(source_surface);
    (void)registry.release(source_surface);
    source.clear_surface_handle();
}

TEST_CASE("strict Vulkan clipped transforms publish transparent native surfaces") {
    using namespace chronon3d;
    using namespace chronon3d::runtime;

    backends::vulkan::VulkanBackend backend;
    RenderSurfaceRegistry registry;
    constexpr int size = 8;
    const auto desc = SurfaceDesc::make(size, size, PixelFormat::Rgba32Float,
                                        ResourceUsage::Storage, LifetimeClass::FrameTransient);
    const auto source_handle = registry.create(desc);
    REQUIRE(backend.create_surface(source_handle, desc).ok());
    REQUIRE(backend.upload_surface(source_handle, desc,
                                   std::vector<float>(size * size * 4, 1.0f)).ok());
    Framebuffer source(size, size, static_cast<Color*>(nullptr));
    source.set_surface_handle(source_handle);
    source.mark_gpu_authoritative();
    const std::array<FramebufferRef, 1> inputs{FramebufferRef{source}};

    for (int mode = 0; mode < 4; ++mode) {
        CAPTURE(mode);
        graph::RenderGraphContext ctx;
        ctx.frame_input.width = size;
        ctx.frame_input.height = size;
        ctx.policy.require_native_gpu = true;
        ctx.backend = &backend;
        ctx.surface_registry = &registry;
        Mat4 matrix{1.0f};
        matrix[3][0] = 0.25f; // Avoid the identity-copy fast path.
        std::array<std::optional<raster::BBox>, 1> bounds{};
        if (mode == 0) bounds[0] = raster::BBox{20, 20, 22, 22};
        if (mode == 1) matrix[3][3] = -1.0f; // Entire quad behind w=0.
        if (mode >= 2) ctx.node_exec.clip_rect = raster::BBox{20, 20, 22, 22};
        RenderSurfaceHandle encode_handle = kInvalidRenderSurfaceHandle;
        if (mode == 3) {
            encode_handle = registry.create(desc);
            REQUIRE(backend.create_surface(encode_handle, desc).ok());
            REQUIRE(backend.upload_surface(encode_handle, desc,
                                           std::vector<float>(size * size * 4, 1.0f)).ok());
            ctx.policy.native_video_encode_surface = encode_handle;
            ctx.node_exec.terminal_video_encode_surface = encode_handle;
        }
        graph::TransformNode node(matrix, 1.0f, SamplingMode::Nearest);
        auto result = node.execute(ctx, inputs, bounds);
        REQUIRE(result.ok());
        auto output = result.take_value();
        REQUIRE(output);
        REQUIRE(output->surface_handle() != kInvalidRenderSurfaceHandle);
        CHECK(output->is_gpu_authoritative());
        std::vector<float> actual(output->width() * output->height() * 4, -1.0f);
        REQUIRE(backend.download_surface(output->surface_handle(), actual).ok());
        CHECK(std::all_of(actual.begin(), actual.end(), [](float v) { return v == 0.0f; }));
        const auto handle = output->surface_handle();
        output->clear_surface_handle();
        REQUIRE(backend.release_surface(handle).ok());
        (void)registry.release(handle);
    }
    source.clear_surface_handle();
    REQUIRE(backend.release_surface(source_handle).ok());
    (void)registry.release(source_handle);
}

TEST_CASE("Vulkan native affine transform matches inverse mapping") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc source_desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const chronon3d::runtime::SurfaceDesc destination_desc{
        4, 4, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> source{
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<float> output(64, -1.0f);
    chronon3d::runtime::SurfaceAffineTransform transform{};
    transform.source_x[0] = 1.0f;
    transform.source_x[3] = 0.0f;
    transform.source_y[1] = 1.0f;
    transform.source_y[3] = 0.0f;
    transform.max_x = 2.0f;
    transform.max_y = 2.0f;
    transform.opacity = 0.5f;
    transform.bilinear = 0;

    REQUIRE(backend.create_surface(301, source_desc).ok());
    REQUIRE(backend.create_surface(302, destination_desc).ok());
    REQUIRE(backend.upload_surface(301, source_desc, source).ok());
    REQUIRE(backend.transform_surface_affine(302, 301, transform).ok());
    REQUIRE(backend.download_surface(302, output).ok());
    CHECK(output[0] == doctest::Approx(0.5f));
    CHECK(output[3] == doctest::Approx(0.5f));
    CHECK(output[(3 * 4 + 3) * 4] == doctest::Approx(0.0f));
}

TEST_CASE("Vulkan native affine transform rotates an image without a CPU fallback") {
    using namespace chronon3d;
    backends::vulkan::VulkanBackend backend;
    const runtime::SurfaceDesc desc{
        2, 2, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> source{
        1.0f, 0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f, 0.0f, 1.0f};
    std::vector<float> output(16, -1.0f);
    runtime::SurfaceAffineTransform transform{};
    // Inverse quarter-turn about the 2x2 image centre. For a destination
    // pixel centre (x+0.5,y+0.5), sample (y+0.5, 1.5-x).
    runtime::set_canvas_source_mapping(
        transform, 0.0f, 1.0f, 0.0f,
        -1.0f, 0.0f, 2.0f,
        runtime::RectF{0.0f, 0.0f, 2.0f, 2.0f});
    transform.bilinear = 0;

    REQUIRE(backend.create_surface(0xC3D301u, desc).ok());
    REQUIRE(backend.create_surface(0xC3D302u, desc).ok());
    REQUIRE(backend.upload_surface(0xC3D301u, desc, source).ok());
    REQUIRE(backend.initialize_transparent_surface(0xC3D302u).ok());
    REQUIRE(backend.transform_surface_affine(0xC3D302u, 0xC3D301u, transform).ok());
    REQUIRE(backend.download_surface(0xC3D302u, output).ok());
    const std::array<std::size_t, 4> source_pixel_for_destination{2, 0, 3, 1};
    for (std::size_t destination_pixel = 0; destination_pixel < 4; ++destination_pixel) {
        for (std::size_t channel = 0; channel < 4; ++channel) {
            CHECK(output[destination_pixel * 4 + channel] ==
                  doctest::Approx(source[source_pixel_for_destination[destination_pixel] * 4 + channel]));
        }
    }
}

TEST_CASE("Vulkan affine bilinear sampling replicates a tight-surface edge like software") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc source_desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const chronon3d::runtime::SurfaceDesc destination_desc{
        1, 1, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> source{
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f};
    std::vector<float> output(4, -1.0f);
    chronon3d::runtime::SurfaceAffineTransform transform{};
    transform.source_x[0] = 1.0f;
    transform.source_y[1] = 1.0f;
    transform.max_x = 2.0f;
    transform.max_y = 2.0f;
    transform.opacity = 1.0f;
    transform.bilinear = 1;
    // The destination pixel centre is (0.5, 0.5); offset the mapped source
    // coordinate to 0.25 so bilinear taps cross the tight surface boundary.
    // source_[xy][3] stores the lower bounds, not the mapping offset.
    transform.source_x[2] = -0.25f;
    transform.source_y[2] = -0.25f;

    REQUIRE(backend.create_surface(303, source_desc).ok());
    REQUIRE(backend.create_surface(304, destination_desc).ok());
    REQUIRE(backend.upload_surface(303, source_desc, source).ok());
    REQUIRE(backend.initialize_transparent_surface(304).ok());
    REQUIRE(backend.transform_surface_affine(304, 303, transform).ok());
    REQUIRE(backend.download_surface(304, output).ok());
    // The mapped coordinate is inside the source rect. Bilinear taps that
    // cross the physical surface edge must clamp to the opaque edge texel,
    // matching blend2d_bridge::sample_bilinear_float.
    CHECK(output[0] == doctest::Approx(1.0f));
    CHECK(output[3] == doctest::Approx(1.0f));
}

TEST_CASE("Vulkan affine bilinear edge replication survives an aligned quarter-scale transform") {
    using namespace chronon3d;
    backends::vulkan::VulkanBackend backend;
    const runtime::SurfaceDesc source_desc{
        8, 8, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage, runtime::LifetimeClass::FrameTransient, 0};
    const runtime::SurfaceDesc destination_desc{
        2, 2, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage, runtime::LifetimeClass::FrameTransient, 0};
    std::vector<float> source(8 * 8 * 4, 0.0f);
    source[0] = 0.75f;
    source[1] = 0.25f;
    source[2] = 0.125f;
    source[3] = 1.0f;
    std::vector<float> output(2 * 2 * 4, -1.0f);
    runtime::SurfaceAffineTransform transform{};
    transform.source_x[0] = 4.0f;
    transform.source_x[2] = -1.75f;
    transform.source_y[1] = 4.0f;
    transform.source_y[2] = -1.75f;
    transform.max_x = 8.0f;
    transform.max_y = 8.0f;
    transform.bilinear = 1u;

    REQUIRE(backend.create_surface(311, source_desc).ok());
    REQUIRE(backend.create_surface(312, destination_desc).ok());
    REQUIRE(backend.upload_surface(311, source_desc, source).ok());
    REQUIRE(backend.initialize_transparent_surface(312).ok());
    REQUIRE(backend.transform_surface_affine(312, 311, transform).ok());
    REQUIRE(backend.download_surface(312, output).ok());
    // The top-left destination centre maps to (0.25,0.25) in the 8x8 source.
    // This keeps the source/destination scale exactly 4:1 and catches a
    // transparent bilinear tap at the image boundary.
    CHECK(output[0] == doctest::Approx(0.75f));
    CHECK(output[1] == doctest::Approx(0.25f));
    CHECK(output[2] == doctest::Approx(0.125f));
    CHECK(output[3] == doctest::Approx(1.0f));
}

TEST_CASE("Vulkan affine projective bilinear sampling clamps taps at fractional source bounds") {
    using namespace chronon3d;
    backends::vulkan::VulkanBackend backend;
    const runtime::SurfaceDesc source_desc{
        2, 2, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage, runtime::LifetimeClass::FrameTransient, 0};
    const runtime::SurfaceDesc destination_desc{
        1, 1, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage, runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> source{
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.5f, 0.25f, 1.0f};
    std::vector<float> output(4, -1.0f);
    runtime::SurfaceAffineTransform transform{};
    // At canvas centre (0.5,0.5), the projective mapping resolves to (1.7,1.7).
    // This is inside the fractional crop [0,1.75), but its bilinear taps cross
    // the physical right and bottom edges of this tight surface.
    transform.source_x[0] = 0.1f;
    transform.source_x[2] = 1.65f;
    transform.source_y[1] = 0.1f;
    transform.source_y[2] = 1.65f;
    transform.max_x = 1.75f;
    transform.max_y = 1.75f;
    transform.bilinear = 1u;
    transform.padding[0] = std::bit_cast<std::uint32_t>(0.05f);
    transform.padding[2] = std::bit_cast<std::uint32_t>(0.975f);

    REQUIRE(backend.create_surface(309, source_desc).ok());
    REQUIRE(backend.create_surface(310, destination_desc).ok());
    REQUIRE(backend.upload_surface(309, source_desc, source).ok());
    REQUIRE(backend.initialize_transparent_surface(310).ok());
    REQUIRE(backend.transform_surface_affine(310, 309, transform).ok());
    REQUIRE(backend.download_surface(310, output).ok());
    CHECK(output[0] == doctest::Approx(1.0f));
    CHECK(output[1] == doctest::Approx(0.5f));
    CHECK(output[2] == doctest::Approx(0.25f));
    CHECK(output[3] == doctest::Approx(1.0f));
}

TEST_CASE("Vulkan affine SourceOver preserves seeded destination outside projected source") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        1, 1, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> source{0.0f, 0.0f, 0.0f, 0.0f};
    const std::vector<float> seeded{0.4f, 0.5f, 0.6f, 1.0f};
    std::vector<float> output(4, -1.0f);
    chronon3d::runtime::SurfaceAffineTransform transform{};
    transform.source_x[0] = 1.0f;
    transform.source_x[2] = -4.0f; // Outside source bounds: zero coverage.
    transform.source_y[1] = 1.0f;
    transform.max_x = 1.0f;
    transform.max_y = 1.0f;
    transform.bilinear = 0;
    transform.blend_source_over = 1;

    REQUIRE(backend.create_surface(305, desc).ok());
    REQUIRE(backend.create_surface(306, desc).ok());
    REQUIRE(backend.upload_surface(305, desc, source).ok());
    REQUIRE(backend.upload_surface(306, desc, seeded).ok());
    REQUIRE(backend.transform_surface_affine(306, 305, transform).ok());
    REQUIRE(backend.download_surface(306, output).ok());
    for (std::size_t channel = 0; channel < 4; ++channel) {
        CHECK(output[channel] == doctest::Approx(seeded[channel]));
    }
}

TEST_CASE("Vulkan affine transform applies an unnormalized 2.5D denominator") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc source_desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const chronon3d::runtime::SurfaceDesc destination_desc{
        1, 1, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> source{
        1.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 0.0f, 1.0f};
    std::vector<float> output(4, 0.0f);
    chronon3d::runtime::SurfaceAffineTransform transform{};
    transform.source_x[0] = 0.001f;
    transform.source_x[2] = -0.125066f;
    transform.source_y[1] = 0.001f;
    transform.source_y[2] = -0.070066f;
    transform.max_x = 2.0f;
    transform.max_y = 2.0f;
    transform.bilinear = 1;
    transform.destination_origin_x = 125;
    transform.destination_origin_y = 70;
    transform.padding[2] = std::bit_cast<std::uint32_t>(0.000868f);

    REQUIRE(backend.create_surface(305, source_desc).ok());
    REQUIRE(backend.create_surface(306, destination_desc).ok());
    REQUIRE(backend.upload_surface(305, source_desc, source).ok());
    REQUIRE(backend.initialize_transparent_surface(306).ok());
    REQUIRE(backend.transform_surface_affine(306, 305, transform).ok());
    REQUIRE(backend.download_surface(306, output).ok());
    CHECK(output[0] == doctest::Approx(1.0f));
    CHECK(output[1] == doctest::Approx(1.0f));
    CHECK(output[2] == doctest::Approx(0.0f));
    CHECK(output[3] == doctest::Approx(1.0f));
}

TEST_CASE("Vulkan affine projective transform samples the animated 2.5D canvas centre with a clipped dispatch") {
    using namespace chronon3d;
    backends::vulkan::VulkanBackend backend;
    constexpr std::uint32_t source_width = 1920;
    constexpr std::uint32_t source_height = 1080;
    constexpr std::uint32_t destination_width = 1670;
    constexpr std::uint32_t destination_height = 940;
    const runtime::SurfaceDesc source_desc{
        source_width, source_height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage, runtime::LifetimeClass::FrameTransient, 0};
    const runtime::SurfaceDesc destination_desc{
        destination_width, destination_height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage, runtime::LifetimeClass::FrameTransient, 0};
    std::vector<float> source(static_cast<std::size_t>(source_width) * source_height * 4);
    for (std::size_t pixel = 0; pixel < source.size() / 4; ++pixel) {
        source[pixel * 4 + 0] = 0.02f;
        source[pixel * 4 + 1] = 0.03f;
        source[pixel * 4 + 2] = 0.95f;
        source[pixel * 4 + 3] = 1.0f;
    }
    for (std::uint32_t y = 0; y < source_height; ++y) {
        for (std::uint32_t x = 920; x < 1000; ++x) {
            const auto pixel = (static_cast<std::size_t>(y) * source_width + x) * 4;
            source[pixel + 0] = 0.956f;
            source[pixel + 1] = 0.831f;
            source[pixel + 2] = 0.021f;
        }
    }
    std::vector<float> output(static_cast<std::size_t>(destination_width) * destination_height * 4);
    runtime::SurfaceAffineTransform transform{};
    runtime::set_canvas_source_mapping(
        transform,
        0.001f, 0.0f, -0.12594755f,
        0.0f, 0.001f, -0.070845529f,
        runtime::RectF{0.0f, 0.0f, static_cast<float>(source_width),
                       static_cast<float>(source_height)});
    transform.opacity = 1.0f;
    transform.bilinear = 1u;
    transform.destination_origin_x = 125;
    transform.destination_origin_y = 70;
    transform.clip_enabled = 1u;
    transform.clip_rect[0] = 0;
    transform.clip_rect[1] = 0;
    transform.clip_rect[2] = static_cast<std::int32_t>(destination_width);
    transform.clip_rect[3] = static_cast<std::int32_t>(destination_height);
    transform.padding[2] = std::bit_cast<std::uint32_t>(0.00086880458f);

    REQUIRE(backend.create_surface(307, source_desc).ok());
    REQUIRE(backend.create_surface(308, destination_desc).ok());
    REQUIRE(backend.upload_surface(307, source_desc, source).ok());
    backend.begin_frame_batch();
    REQUIRE(backend.initialize_transparent_surface(308).ok());
    REQUIRE(backend.transform_surface_affine(308, 307, transform).ok());
    backend.end_frame_batch();
    REQUIRE(backend.download_surface(308, output).ok());
    const auto center = (static_cast<std::size_t>(destination_height / 2) *
                         destination_width + destination_width / 2) * 4;
    CHECK(output[center + 0] == doctest::Approx(0.956f).epsilon(0.01f));
    CHECK(output[center + 1] == doctest::Approx(0.831f).epsilon(0.01f));
    CHECK(output[center + 2] == doctest::Approx(0.021f).epsilon(0.01f));
    CHECK(output[center + 3] == doctest::Approx(1.0f).epsilon(0.01f));
}

TEST_CASE("Vulkan partial upload clears a recycled surface before the first write") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        4, 4, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> opaque(4 * 4 * 4, 1.0f);
    const std::vector<float> pixel{0.25f, 0.5f, 0.75f, 0.5f};
    std::vector<float> output(4 * 4 * 4, -1.0f);

    REQUIRE(backend.create_surface(305, desc).ok());
    REQUIRE(backend.create_surface(306, desc).ok());
    REQUIRE(backend.upload_surface(305, desc, opaque).ok());
    REQUIRE(backend.release_surface(305).ok());
    REQUIRE(backend.upload_surface_region(306, desc, 1, 1, 1, 1, pixel).ok());
    REQUIRE(backend.download_surface(306, output).ok());
    for (std::size_t index = 0; index < 16; ++index) {
        const auto alpha = output[index * 4 + 3];
        if (index == 5) {
            CHECK(alpha == doctest::Approx(0.5f));
        } else {
            CHECK(alpha == doctest::Approx(0.0f));
        }
    }
}

TEST_CASE("Vulkan separable blur stays on device across two passes") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    constexpr std::uint32_t size = 9;
    const chronon3d::runtime::SurfaceDesc desc{
        size, size, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    std::vector<float> source(size * size * 4, 0.0f);
    const auto center = (static_cast<std::size_t>(size / 2) * size + size / 2) * 4;
    source[center + 0] = 1.0f;
    source[center + 3] = 1.0f;
    std::vector<float> output(source.size(), 0.0f);

    REQUIRE(backend.create_surface(401, desc).ok());
    REQUIRE(backend.create_surface(402, desc).ok());
    REQUIRE(backend.create_surface(403, desc).ok());
    REQUIRE(backend.upload_surface(401, desc, source).ok());
    REQUIRE(backend.blur_surface(402, 401, 1.5f, true, false).ok());
    REQUIRE(backend.blur_surface(403, 402, 1.5f, false, false).ok());
    REQUIRE(backend.download_surface(403, output).ok());

    const auto neighbor = (static_cast<std::size_t>(size / 2) * size + size / 2 + 1) * 4;
    CHECK(output[center + 0] < 1.0f);
    CHECK(output[neighbor + 0] > 0.0f);
    CHECK(output[neighbor + 0] < output[center + 0]);
}

TEST_CASE("Vulkan transparent initialization replaces recycled opaque pixels") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        8, 8, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    std::vector<float> opaque(8 * 8 * 4, 1.0f);
    std::vector<float> output(opaque.size(), -1.0f);
    REQUIRE(backend.create_surface(415, desc).ok());
    REQUIRE(backend.upload_surface(415, desc, opaque).ok());
    REQUIRE(backend.initialize_transparent_surface(415).ok());
    REQUIRE(backend.download_surface(415, output).ok());
    for (const auto component : output) CHECK(component == 0.0f);
}

TEST_CASE("Vulkan glow sequence preserves source and composites halo behind it") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    constexpr std::uint32_t size = 9;
    const chronon3d::runtime::SurfaceDesc desc{
        size, size, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    std::vector<float> source(size * size * 4, 0.0f);
    const auto center = (static_cast<std::size_t>(size / 2) * size + size / 2) * 4;
    source[center + 0] = 0.8f;
    source[center + 3] = 1.0f;
    std::vector<float> output(source.size(), 0.0f);

    REQUIRE(backend.create_surface(411, desc).ok());
    REQUIRE(backend.create_surface(412, desc).ok());
    REQUIRE(backend.create_surface(413, desc).ok());
    REQUIRE(backend.create_surface(414, desc).ok());
    REQUIRE(backend.upload_surface(411, desc, source).ok());
    REQUIRE(backend.upload_surface(413, desc, source).ok());
    const auto submissions_before = backend.stats().submissions;
    REQUIRE(backend.glow_surfaces(
        413, 411, 412, 414, 1.5f, 1.0f,
        chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f}).ok());
    CHECK(backend.stats().submissions == submissions_before + 1);
    REQUIRE(backend.download_surface(413, output).ok());

    CHECK(output[center + 0] == doctest::Approx(source[center + 0]));
    const auto neighbor = (static_cast<std::size_t>(size / 2) * size + size / 2 + 1) * 4;
    CHECK(output[neighbor + 0] > 0.0f);

    // The pass descriptor sets are persistent/reusable across frames; a
    // second one-submission glow must not exhaust or reallocate the pool.
    const auto second_before = backend.stats().submissions;
    REQUIRE(backend.glow_surfaces(
        413, 411, 412, 414, 1.5f, 1.0f,
        chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f}).ok());
    CHECK(backend.stats().submissions == second_before + 1);
}

TEST_CASE("Vulkan frame batch records every pass into a single submission") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        4, 4, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    std::vector<float> source(4 * 4 * 4, 0.0f);
    const auto center = (static_cast<std::size_t>(2) * 4 + 2) * 4;
    source[center + 0] = 0.8f;
    source[center + 3] = 1.0f;
    std::vector<float> output(source.size(), 0.0f);

    REQUIRE(backend.create_surface(521, desc).ok());
    REQUIRE(backend.create_surface(522, desc).ok());
    REQUIRE(backend.create_surface(523, desc).ok());
    REQUIRE(backend.create_surface(524, desc).ok());
    REQUIRE(backend.upload_surface(521, desc, source).ok());

    const auto submissions_before = backend.stats().submissions;
    backend.begin_frame_batch();
    REQUIRE(backend.transform_surface(522, 521, 0, 0, 1.0f).ok());
    REQUIRE(backend.blur_surface(523, 522, 1.5f, true, false).ok());
    REQUIRE(backend.color_adjust_surface(
        524, 523, 0.0f, 1.0f,
        chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f).ok());
    REQUIRE(backend.composite_surfaces(
        524, 521, chronon3d::BlendMode::Normal,
        chronon3d::CompositeOperator::SourceOver).ok());
    backend.end_frame_batch();
    // Four recorded passes must coalesce into exactly one queue submission:
    // the whole frame is submitted once, not once per operation.
    CHECK(backend.stats().submissions == submissions_before + 1);

    REQUIRE(backend.download_surface(524, output).ok());
    // The transform → blur → color adjust → composite chain preserved the
    // bright opaque center pixel.
    CHECK(output[center + 0] > 0.0f);
    CHECK(output[center + 3] > 0.0f);

    // The frame-batch ring keeps one fence per slot: four consecutive
    // batches rotate through all three slots, and the last one reuses a
    // slot whose previous submission is still in flight — begin_frame_batch()
    // must wait on that slot's fence instead of stalling the whole device.
    const auto ring_before = backend.stats().submissions;
    for (int i = 0; i < 4; ++i) {
        backend.begin_frame_batch();
        REQUIRE(backend.transform_surface(522, 521, 0, 0, 1.0f).ok());
        backend.end_frame_batch();
    }
    CHECK(backend.stats().submissions == ring_before + 4);
    REQUIRE(backend.download_surface(522, output).ok());
    CHECK(output[center + 0] == doctest::Approx(0.8f).epsilon(1e-3f));
}

TEST_CASE("Vulkan native occlusion holds a mesh half behind a 2.5D plane in both authoring orders") {
    using namespace chronon3d;
    using namespace chronon3d::runtime;

    // The Vulkan half of the mesh/plane canary. A GPU-only layer has no CPU
    // depth map, so the graph path is: inline-mesh rasterizer publishes
    // per-pixel camera depth -> occlusion_tag compares it with the depth
    // already composited this frame -> matte removes occluded pixels ->
    // source-over composite. This test drives exactly that sequence through
    // the public surface API for both authoring orders; a per-layer Z sort
    // would produce one wrong probe in one of the two orders.
    backends::vulkan::VulkanBackend backend;
    RenderSurfaceRegistry surfaces;
    const auto format = make_frame_format(
        PixelFormat::Rgba32Float, {}, runtime::AlphaMode::Premultiplied);
    const auto layer_desc = SurfaceDesc::make(
        64, 64, format, ResourceUsage::Storage, LifetimeClass::FrameTransient);
    const auto canvas = surfaces.create(layer_desc);
    const auto plane = surfaces.create(layer_desc);
    const auto mesh = surfaces.create(layer_desc);
    const auto plane_depth = surfaces.create(layer_desc);
    const auto mesh_depth = surfaces.create(layer_desc);
    const auto frame_depth = surfaces.create(layer_desc);
    const auto mask = surfaces.create(layer_desc);
    const auto texture_desc = SurfaceDesc::make(
        1, 1, format, ResourceUsage::Storage, LifetimeClass::JobPersistent);
    const auto texture = surfaces.create(texture_desc);
    for (const auto handle : {canvas, plane, mesh, plane_depth, mesh_depth,
                              frame_depth, mask}) {
        REQUIRE(backend.create_surface(handle, layer_desc).ok());
    }
    REQUIRE(backend.create_surface(texture, texture_desc).ok());
    REQUIRE(backend.upload_surface(texture, texture_desc,
                                   std::array<float, 4>{1.f, 1.f, 1.f, 1.f}).ok());

    // Opaque blue plane at camera depth 40, opaque red tinted white triangle
    // crossing it: left vertex nearer (20), right vertex farther (60).
    constexpr float plane_camera_depth = 40.0f;
    std::vector<float> blue(64U * 64U * 4U, 0.0f);
    for (std::size_t i = 0; i < blue.size(); i += 4) {
        blue[i + 2] = 1.0f;
        blue[i + 3] = 1.0f;
    }
    // Screen x/y, inverse camera depth, UV, per-vertex light.
    const std::array<float, 24> vertices{
        12.f, 12.f, 1.0f / 20.0f, 0.f, 0.f, 1.f, 1.f, 1.f,
        52.f, 12.f, 1.0f / 60.0f, 1.f, 0.f, 1.f, 1.f, 1.f,
        32.f, 52.f, 1.0f / 40.0f, 0.5f, 1.f, 1.f, 1.f, 1.f};
    const std::array<std::uint32_t, 3> indices{0, 1, 2};

    for (const bool plane_first : {true, false}) {
        CAPTURE(plane_first);
        const std::vector<float> clear(64U * 64U * 4U, 0.0f);
        for (const auto handle : {canvas, plane, mesh, plane_depth, mesh_depth,
                                  frame_depth, mask}) {
            REQUIRE(backend.upload_surface(handle, layer_desc, clear).ok());
        }
        REQUIRE(backend.upload_surface(plane, layer_desc, blue).ok());
        // The frame accumulator starts as "nothing composited here yet".
        REQUIRE(backend.dof_depth_stamp_surface(
                    frame_depth, plane, 0.0f, -1.0f, 0, 0, 0, 0, 0, 0, true).ok());

        const auto emit_layer = [&](RenderSurfaceHandle layer,
                                    RenderSurfaceHandle layer_depth,
                                    bool per_pixel) {
            if (!per_pixel) {
                // Flat layer: constant camera depth over its covered pixels.
                REQUIRE(backend.dof_depth_stamp_surface(
                            layer_depth, layer, plane_camera_depth, 0.0f,
                            0, 0, 64, 64, 0, 0, true).ok());
            }
            REQUIRE(backend.occlusion_tag_surface(mask, layer_depth, frame_depth, true).ok());
            if (std::getenv("C3_NATIVE_DIAG") != nullptr) {
                std::vector<float> dbg(64U * 64U * 4U, 0.f);
                REQUIRE(backend.download_surface(mask, dbg).ok());
                MESSAGE("mask n" << dbg[(32 * 64 + 23) * 4 + 3]
                        << " f" << dbg[(32 * 64 + 37) * 4 + 3]);
                REQUIRE(backend.download_surface(frame_depth, dbg).ok());
                MESSAGE("frame n" << dbg[(32 * 64 + 23) * 4]
                        << " f" << dbg[(32 * 64 + 37) * 4]);
                REQUIRE(backend.download_surface(layer_depth, dbg).ok());
                MESSAGE("layer n" << dbg[(32 * 64 + 23) * 4]
                        << "/" << dbg[(32 * 64 + 23) * 4 + 1]
                        << " f" << dbg[(32 * 64 + 37) * 4]
                        << "/" << dbg[(32 * 64 + 37) * 4 + 1]);
            }
            REQUIRE(backend.matte_surface(layer, layer, mask, false, false).ok());
            REQUIRE(backend.composite_surfaces(canvas, layer, BlendMode::Normal,
                                               CompositeOperator::SourceOver,
                                               std::nullopt).ok());
        };
        const auto emit_plane = [&] { emit_layer(plane, plane_depth, false); };
        const auto emit_mesh = [&] {
            // Reset the per-layer stamp, then let the rasterizer publish the
            // nearest camera depth of every covered pixel.
            REQUIRE(backend.dof_depth_stamp_surface(
                        mesh_depth, mesh, 0.0f, -1.0f, 0, 0, 0, 0, 0, 0, true).ok());
            REQUIRE(backend.draw_inline_mesh_surface(
                        mesh, texture, vertices, indices, 0, 0, 64, 64,
                        Color{1.f, 0.f, 0.f, 1.f}, std::nullopt,
                        mesh_depth, 1.0f).ok());
            emit_layer(mesh, mesh_depth, true);
        };
        if (plane_first) { emit_plane(); emit_mesh(); }
        else { emit_mesh(); emit_plane(); }

        std::vector<float> actual(64U * 64U * 4U, 0.0f);
        REQUIRE(backend.download_surface(canvas, actual).ok());
        const auto probe = [&](int x) {
            const auto index = static_cast<std::size_t>((32 * 64 + x) * 4);
            return std::array<float, 4>{actual[index], actual[index + 1],
                                        actual[index + 2], actual[index + 3]};
        };
        const auto near_sample = probe(23);   // mesh half in front of the plane
        const auto far_sample = probe(37);    // mesh half behind the plane
        INFO("native near " << near_sample[0] << "," << near_sample[2] << " far "
             << far_sample[0] << "," << far_sample[2]);
        CHECK(near_sample[0] > 0.8f);
        CHECK(near_sample[2] < 0.1f);
        CHECK(near_sample[3] > 0.8f);
        CHECK(far_sample[2] > 0.8f);
        CHECK(far_sample[0] < 0.1f);
        CHECK(far_sample[3] > 0.8f);
    }

    for (const auto handle : {canvas, plane, mesh, plane_depth, mesh_depth,
                              frame_depth, mask, texture}) {
        REQUIRE(backend.release_surface(handle).ok());
        CHECK(surfaces.release(handle));
    }
}

TEST_CASE("Vulkan disk bokeh kernel matches the CPU kernel within the frozen tolerance") {
    using namespace chronon3d;
    using namespace chronon3d::runtime;

    // C4 numerical-parity gate: the two lanes render the same accepted scene,
    // so the disk kernel must agree with its CPU mirror. Threshold is frozen
    // here before the measurement: 1e-3 absolute per channel, which is well
    // below one 8-bit code value (1/255). Sample layout matches the CPU
    // fixture in tests/renderer/camera/test_per_pixel_dof.cpp: a defocused
    // centre, one bright sample inside the radius-4 disk and one outside it
    // (disk vs square support discriminator), plus highlight weighting.
    constexpr std::uint32_t size = 17;
    constexpr float kMaxRadius = 4.0f;
    constexpr float kHighlightGain = 2.0f;
    constexpr float kTolerance = 1e-3f;

    backends::vulkan::VulkanBackend backend;
    RenderSurfaceRegistry surfaces;
    const auto format = make_frame_format(
        PixelFormat::Rgba32Float, {}, runtime::AlphaMode::Premultiplied);
    const auto desc = SurfaceDesc::make(
        size, size, format, ResourceUsage::Storage, LifetimeClass::FrameTransient);
    const auto source_handle = surfaces.create(desc);
    const auto depth_handle = surfaces.create(desc);
    const auto destination_handle = surfaces.create(desc);
    for (const auto handle : {source_handle, depth_handle, destination_handle}) {
        REQUIRE(backend.create_surface(handle, desc).ok());
    }

    const std::size_t pixels = static_cast<std::size_t>(size) * size;
    std::vector<Color> source(pixels, Color::transparent());
    std::vector<float> depth(pixels, 100.0f);
    std::vector<float> radii(pixels, kMaxRadius);
    const auto index = [](std::uint32_t x, std::uint32_t y) {
        return static_cast<std::size_t>(y) * size + x;
    };
    source[index(8, 8)] = Color{0.2f, 0.2f, 0.2f, 1.0f};
    source[index(12, 8)] = Color{4.0f, 4.0f, 4.0f, 1.0f};
    source[index(12, 12)] = Color{1.0f, 0.0f, 0.0f, 1.0f};
    std::vector<float> source_pixels(pixels * 4, 0.0f);
    std::vector<float> depth_pixels(pixels * 4, 0.0f);
    Framebuffer cpu_frame(static_cast<int>(size), static_cast<int>(size));
    for (std::size_t p = 0; p < pixels; ++p) {
        source_pixels[p * 4 + 0] = source[p].r;
        source_pixels[p * 4 + 1] = source[p].g;
        source_pixels[p * 4 + 2] = source[p].b;
        source_pixels[p * 4 + 3] = source[p].a;
        // Shader encoding: r = positive camera depth, g = blur radius.
        depth_pixels[p * 4 + 0] = depth[p];
        depth_pixels[p * 4 + 1] = radii[p];
        cpu_frame.set_pixel(static_cast<int>(p % size), static_cast<int>(p / size),
                            source[p]);
    }
    REQUIRE(backend.upload_surface(source_handle, desc, source_pixels).ok());
    REQUIRE(backend.upload_surface(depth_handle, desc, depth_pixels).ok());
    REQUIRE(backend.dof_blur_disk_surface(
                destination_handle, source_handle, depth_handle,
                0, 0, static_cast<int>(size), static_cast<int>(size),
                kMaxRadius, false, kHighlightGain).ok());
    std::vector<float> actual(pixels * 4, 0.0f);
    REQUIRE(backend.download_surface(destination_handle, actual).ok());

    renderer::apply_per_pixel_dof_disk(
        cpu_frame, std::span<const float>{depth}, std::span<const float>{radii},
        0, 0, static_cast<int>(size), static_cast<int>(size),
        kMaxRadius, false, kHighlightGain);

    float max_delta = 0.0f;
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const auto expected = cpu_frame.get_pixel(static_cast<int>(x),
                                                      static_cast<int>(y));
            const auto base = index(x, y) * 4;
            const std::array<float, 4> deltas{
                std::abs(actual[base + 0] - expected.r),
                std::abs(actual[base + 1] - expected.g),
                std::abs(actual[base + 2] - expected.b),
                std::abs(actual[base + 3] - expected.a)};
            for (const float delta : deltas) max_delta = std::max(max_delta, delta);
            CAPTURE(x);
            CAPTURE(y);
            CHECK(deltas[0] <= kTolerance);
            CHECK(deltas[1] <= kTolerance);
            CHECK(deltas[2] <= kTolerance);
            CHECK(deltas[3] <= kTolerance);
        }
    }
    MESSAGE("Vulkan/CPU disk bokeh max channel delta: " << max_delta
            << " (threshold " << kTolerance << ")");

    for (const auto handle : {source_handle, depth_handle, destination_handle}) {
        REQUIRE(backend.release_surface(handle).ok());
        CHECK(surfaces.release(handle));
    }
}
#endif
