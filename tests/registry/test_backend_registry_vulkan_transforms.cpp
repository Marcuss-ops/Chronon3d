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
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/resource_plan.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/runtime/overlay_template.hpp>
#include <chronon3d/render_graph/checkbackend.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/backends/software/software_compositor.hpp>
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

TEST_CASE("Vulkan glow sequence uses blur passes followed by additive composite") {
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

    CHECK(output[center + 0] > source[center + 0]);
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
#endif
