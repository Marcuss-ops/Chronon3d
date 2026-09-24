// test_backend_registry_vulkan_surfaces.cpp — Split from
// test_backend_registry_vulkan.cpp: color-adjust / EffectStackNode /
// composite / source-over parity surface coverage (Vulkan-gated as in
// the origin).
#include <doctest/doctest.h>

#include <array>
#include <algorithm>
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
TEST_CASE("Vulkan color adjust matches the CPU scalar oracle") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    constexpr std::uint32_t width = 2;
    constexpr std::uint32_t height = 2;
    const chronon3d::runtime::SurfaceDesc desc{
        width, height, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> source{
        0.2f, 0.4f, 0.6f, 1.0f,
        0.8f, 0.1f, 0.3f, 0.5f,
        0.9f, 0.9f, 0.9f, 0.0f,
        0.0f, 0.2f, 1.0f, 1.0f};
    std::vector<float> actual(source.size(), 0.0f);
    REQUIRE(backend.create_surface(421, desc).ok());
    REQUIRE(backend.create_surface(422, desc).ok());
    REQUIRE(backend.upload_surface(422, desc, source).ok());
    REQUIRE(backend.color_adjust_surface(
        421, 422, 0.1f, 1.5f,
        chronon3d::Color{0.2f, 0.8f, 0.4f, 1.0f}, 0.25f).ok());
    REQUIRE(backend.download_surface(421, actual).ok());

    for (std::size_t pixel = 0; pixel < source.size() / 4; ++pixel) {
        const auto base = pixel * 4;
        if (source[base + 3] == 0.0f) {
            CHECK(actual[base + 0] == doctest::Approx(source[base + 0]));
            CHECK(actual[base + 1] == doctest::Approx(source[base + 1]));
            CHECK(actual[base + 2] == doctest::Approx(source[base + 2]));
            CHECK(actual[base + 3] == doctest::Approx(0.0f));
            continue;
        }
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const float adjusted = std::clamp(
                (source[base + channel] + 0.1f - 0.5f) * 1.5f + 0.5f,
                0.0f, 1.0f);
            const float tint = channel == 0 ? 0.2f : (channel == 1 ? 0.8f : 0.4f);
            CHECK(actual[base + channel] == doctest::Approx(
                adjusted * 0.75f + tint * 0.25f).epsilon(1e-5));
        }
        CHECK(actual[base + 3] == doctest::Approx(source[base + 3]));
    }
}

TEST_CASE("Vulkan lighting preserves alpha and matches the CPU multiplier oracle") {
    using namespace chronon3d;

    backends::vulkan::VulkanBackend backend;
    constexpr std::uint32_t width = 2;
    constexpr std::uint32_t height = 2;
    const runtime::SurfaceDesc desc{
        width, height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> source{
        0.2f, 0.4f, 0.6f, 1.0f,
        0.8f, 0.1f, 0.3f, 0.5f,
        0.9f, 0.9f, 0.9f, 0.0f,
        0.0f, 0.2f, 1.0f, 1.0f};
    const Color multiplier{0.25f, 0.5f, 0.75f, 1.0f};
    std::vector<float> actual(source.size(), 0.0f);

    REQUIRE(backend.create_surface(461, desc).ok());
    REQUIRE(backend.create_surface(462, desc).ok());
    REQUIRE(backend.upload_surface(462, desc, source).ok());
    REQUIRE(backend.lighting_surface(461, 462, multiplier).ok());
    REQUIRE(backend.download_surface(461, actual).ok());

    for (std::size_t pixel = 0; pixel < source.size() / 4; ++pixel) {
        const auto base = pixel * 4;
        if (source[base + 3] == 0.0f) {
            CHECK(actual[base + 0] == doctest::Approx(source[base + 0]));
            CHECK(actual[base + 1] == doctest::Approx(source[base + 1]));
            CHECK(actual[base + 2] == doctest::Approx(source[base + 2]));
        } else {
            CHECK(actual[base + 0] == doctest::Approx(source[base + 0] * multiplier.r));
            CHECK(actual[base + 1] == doctest::Approx(source[base + 1] * multiplier.g));
            CHECK(actual[base + 2] == doctest::Approx(source[base + 2] * multiplier.b));
        }
        CHECK(actual[base + 3] == doctest::Approx(source[base + 3]));
    }
}

TEST_CASE("EffectStackNode dispatches a full-frame glow through Vulkan surfaces") {
    using namespace chronon3d;
    using namespace chronon3d::graph;

    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;

    RenderGraphContext ctx;
    ctx.frame_input.width = 8;
    ctx.frame_input.height = 8;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;

    auto source = std::make_shared<Framebuffer>(8, 8);
    source->clear(Color::transparent());
    source->set_pixel(4, 4, Color{1.0f, 0.5f, 0.25f, 1.0f});

    GlowParams params;
    params.radius = 2.0f;
    params.intensity = 0.5f;
    params.color = Color{0.25f, 0.5f, 1.0f, 1.0f};
    // The native glow path uses the same single-pass contract as software.
    EffectStack effects;
    effects.push_back(effects::EffectInstance{params});
    EffectStackNode node(std::move(effects));

    const auto before = backend.stats();
    const std::array<FramebufferRef, 1> inputs{source.get()};
    const std::array<std::optional<raster::BBox>, 1> bboxes{
        raster::BBox{0, 0, 8, 8}};
    const auto result = node.execute(ctx, inputs, bboxes);

    REQUIRE(result.ok());
    REQUIRE(result.value() != nullptr);
    const auto handle = result.value()->surface_handle();
    REQUIRE(handle != runtime::kInvalidRenderSurfaceHandle);
    CHECK(surfaces.size() == 1); // output survives; H/V scratch is released
    CHECK(backend.stats().submissions > before.submissions);

    std::vector<float> output(8 * 8 * 4, 0.0f);
    REQUIRE(backend.download_surface(handle, output).ok());
    const auto center = static_cast<std::size_t>((4 * 8 + 4) * 4);
    const auto neighbor = static_cast<std::size_t>((4 * 8 + 5) * 4);
    CHECK(output[center + 0] > 1.0f);
    CHECK(output[neighbor + 0] > 0.0f);

    REQUIRE(backend.release_surface(handle).ok());
    CHECK(surfaces.release(handle));
}

TEST_CASE("EffectStackNode glow reads an available native surface when CPU mirror is stale") {
    using namespace chronon3d;
    using namespace chronon3d::graph;

    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.frame_input.width = 8;
    ctx.frame_input.height = 8;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;

    const runtime::SurfaceDesc desc{
        2, 2, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::FrameTransient, 0};
    const auto source_handle = surfaces.create(desc);
    REQUIRE(source_handle != runtime::kInvalidRenderSurfaceHandle);
    REQUIRE(backend.create_surface(source_handle, desc).ok());
    std::vector<float> pixels(2 * 2 * 4, 0.0f);
    const auto center = static_cast<std::size_t>((1 * 2 + 1) * 4);
    pixels[center + 0] = 1.0f;
    pixels[center + 3] = 1.0f;
    REQUIRE(backend.upload_surface(source_handle, desc, pixels).ok());

    auto source = std::make_shared<Framebuffer>(2, 2);
    source->set_origin(3, 3);
    source->clear(Color::transparent()); // deliberately stale CPU mirror
    source->set_surface_handle(source_handle);
    GlowParams params{.radius = 1.5f, .intensity = 0.5f};
    EffectStack effects;
    effects.push_back(effects::EffectInstance{params});
    EffectStackNode node(std::move(effects));
    const std::array<FramebufferRef, 1> inputs{source.get()};
    const std::array<std::optional<raster::BBox>, 1> bboxes{
        raster::BBox{3, 3, 5, 5}};

    const auto result = node.execute(ctx, inputs, bboxes);
    REQUIRE(result.ok());
    const auto output_handle = result.value()->surface_handle();
    REQUIRE(output_handle != runtime::kInvalidRenderSurfaceHandle);
    CHECK(result.value()->width() > source->width());
    CHECK(result.value()->height() > source->height());
    const auto output_x = 4 - result.value()->origin_x();
    const auto output_y = 4 - result.value()->origin_y();
    const auto output_center = static_cast<std::size_t>(
        (output_y * result.value()->width() + output_x) * 4);
    std::vector<float> output(static_cast<std::size_t>(
        result.value()->width() * result.value()->height() * 4), 0.0f);
    REQUIRE(backend.download_surface(output_handle, output).ok());
    CHECK(output[output_center] > 1.0f);
    CHECK(output[output_center + 3] > 0.0f);

    REQUIRE(backend.release_surface(output_handle).ok());
    CHECK(surfaces.release(output_handle));
    REQUIRE(backend.release_surface(source_handle).ok());
    CHECK(surfaces.release(source_handle));
}

TEST_CASE("EffectStackNode blurs a GPU-only input without uploading a CPU framebuffer") {
    using namespace chronon3d;
    using namespace chronon3d::graph;

    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.frame_input.width = 8;
    ctx.frame_input.height = 8;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;
    ctx.policy.require_native_gpu = true;

    const runtime::SurfaceDesc desc{
        8, 8, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::FrameTransient, 0};
    const auto source_handle = surfaces.create(desc);
    REQUIRE(source_handle != runtime::kInvalidRenderSurfaceHandle);
    REQUIRE(backend.create_surface(source_handle, desc).ok());
    std::vector<float> pixels(8 * 8 * 4, 0.0f);
    const auto center = static_cast<std::size_t>((4 * 8 + 4) * 4);
    pixels[center + 0] = 1.0f;
    pixels[center + 1] = 0.5f;
    pixels[center + 2] = 0.25f;
    pixels[center + 3] = 1.0f;
    REQUIRE(backend.upload_surface(source_handle, desc, pixels).ok());

    auto source = std::make_shared<Framebuffer>(8, 8, static_cast<Color*>(nullptr));
    source->set_surface_handle(source_handle);
    BlurParams params{.radius = 2.0f};
    EffectStack effects;
    effects.push_back(effects::EffectInstance{params});
    EffectStackNode node(std::move(effects));
    const std::array<FramebufferRef, 1> inputs{source.get()};
    const std::array<std::optional<raster::BBox>, 1> bboxes{
        raster::BBox{0, 0, 8, 8}};

    const auto result = node.execute(ctx, inputs, bboxes);
    REQUIRE(result.ok());
    REQUIRE(result.value() != nullptr);
    const auto output_handle = result.value()->surface_handle();
    REQUIRE(output_handle != runtime::kInvalidRenderSurfaceHandle);
    CHECK(output_handle != source_handle);
    CHECK(surfaces.size() == 2); // borrowed source + blurred output; H/V scratch released

    std::vector<float> output(8 * 8 * 4, 0.0f);
    REQUIRE(backend.download_surface(output_handle, output).ok());
    CHECK(output[center + 0] > 0.0f);
    CHECK(output[center + 3] > 0.0f);

    REQUIRE(backend.release_surface(output_handle).ok());
    CHECK(surfaces.release(output_handle));
    REQUIRE(backend.release_surface(source_handle).ok());
    CHECK(surfaces.release(source_handle));
}

TEST_CASE("EffectStackNode dispatches the simple glow contract through Vulkan surfaces") {
    using namespace chronon3d;
    using namespace chronon3d::graph;

    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.frame_input.width = 8;
    ctx.frame_input.height = 8;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;

    auto source = std::make_shared<Framebuffer>(8, 8);
    source->clear(Color::transparent());
    source->set_pixel(4, 4, Color{1.0f, 0.5f, 0.25f, 1.0f});

    GlowParams params;
    params.radius = 2.0f;
    params.intensity = 0.5f;
    EffectStack effects;
    effects.push_back(effects::EffectInstance{params});
    EffectStackNode node(std::move(effects));

    const std::array<FramebufferRef, 1> inputs{source.get()};
    const std::array<std::optional<raster::BBox>, 1> bboxes{
        raster::BBox{0, 0, 8, 8}};
    const auto result = node.execute(ctx, inputs, bboxes);
    REQUIRE(result.ok());
    REQUIRE(result.value() != nullptr);
    const auto handle = result.value()->surface_handle();
    REQUIRE(handle != runtime::kInvalidRenderSurfaceHandle);
    REQUIRE(backend.release_surface(handle).ok());
    CHECK(surfaces.release(handle));
}

TEST_CASE("EffectStackNode dispatches a full-frame tint through Vulkan surfaces") {
    using namespace chronon3d;
    using namespace chronon3d::graph;

    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.frame_input.width = 2;
    ctx.frame_input.height = 2;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;

    auto source = std::make_shared<Framebuffer>(2, 2);
    source->set_pixel(0, 0, Color{0.2f, 0.4f, 0.6f, 1.0f});
    source->set_pixel(1, 0, Color{0.8f, 0.1f, 0.3f, 0.5f});
    source->set_pixel(0, 1, Color{0.9f, 0.9f, 0.9f, 0.0f});
    source->set_pixel(1, 1, Color{0.0f, 0.2f, 1.0f, 1.0f});

    EffectStack effects;
    effects.push_back(effects::EffectInstance{
        TintParams{.color = Color{0.2f, 0.8f, 0.4f, 1.0f}, .amount = 0.25f}});
    EffectStackNode node(std::move(effects));
    const std::array<FramebufferRef, 1> inputs{source.get()};
    const std::array<std::optional<raster::BBox>, 1> bboxes{
        raster::BBox{0, 0, 2, 2}};
    const auto result = node.execute(ctx, inputs, bboxes);

    REQUIRE(result.ok());
    REQUIRE(result.value() != nullptr);
    const auto handle = result.value()->surface_handle();
    REQUIRE(handle != runtime::kInvalidRenderSurfaceHandle);
    std::vector<float> output(16, 0.0f);
    REQUIRE(backend.download_surface(handle, output).ok());
    CHECK(output[0] == doctest::Approx(0.2f).epsilon(1e-5));
    CHECK(output[1] == doctest::Approx(0.5f).epsilon(1e-5));
    CHECK(output[2] == doctest::Approx(0.55f).epsilon(1e-5));
    CHECK(output[3] == doctest::Approx(1.0f).epsilon(1e-5));
    CHECK(output[8] == doctest::Approx(0.9f)); // transparent source is preserved

    REQUIRE(backend.release_surface(handle).ok());
    CHECK(surfaces.release(handle));
}

TEST_CASE("Vulkan backend composite_layer is fail-closed without native surfaces") {
    // P0.1 regression lock: the CPU pixel-blit fallback inside the Vulkan
    // backend was demolished. A GPU backend must never blend pixels on the
    // CPU; callers must materialize native surfaces first (CPU-origin assets
    // are legitimate, silent CPU fallbacks are not).
    chronon3d::backends::vulkan::VulkanBackend backend;
    chronon3d::Framebuffer destination(2, 2);
    chronon3d::Framebuffer source(2, 2);
    destination.clear(chronon3d::Color{0.1f, 0.2f, 0.3f, 0.8f});
    source.clear(chronon3d::Color{0.5f, 0.25f, 0.125f, 0.5f});

    bool threw = false;
    try {
        backend.composite_layer(destination, source, chronon3d::BlendMode::Normal,
                                std::nullopt, chronon3d::CompositeOperator::SourceOver);
    } catch (const std::invalid_argument& e) {
        threw = true;
        const std::string message = e.what();
        CHECK(message.find("missing native surface handle") != std::string::npos);
    }
    CHECK(threw);

    // Source-over premultiplied semantics remain covered by the native-surface
    // composite test above and by SoftwareCompositor parity tests.
}

TEST_CASE("Vulkan native surfaces stay device-local across upload and composite") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, 0};
    const std::vector<float> destination(16, 0.0f);
    const std::vector<float> source{
        0.5f, 0.25f, 0.125f, 0.5f,
        0.5f, 0.25f, 0.125f, 0.5f,
        0.5f, 0.25f, 0.125f, 0.5f,
        0.5f, 0.25f, 0.125f, 0.5f};
    std::vector<float> output(16, 0.0f);

    REQUIRE(backend.create_surface(101, desc).ok());
    REQUIRE(backend.create_surface(102, desc).ok());
    REQUIRE(backend.upload_surface(101, desc, destination).ok());
    REQUIRE(backend.upload_surface(102, desc, source).ok());
    REQUIRE(backend.composite_surfaces(
        101, 102, chronon3d::BlendMode::Normal,
        chronon3d::CompositeOperator::SourceOver).ok());
    REQUIRE(backend.download_surface(101, output).ok());
    CHECK(output[0] == doctest::Approx(0.5f));
    CHECK(output[1] == doctest::Approx(0.25f));
    CHECK(output[2] == doctest::Approx(0.125f));
    CHECK(output[3] == doctest::Approx(0.5f));
}

TEST_CASE("Vulkan source-over composite matches SoftwareCompositor oracle") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    chronon3d::Framebuffer cpu_destination(4, 3);
    chronon3d::Framebuffer cpu_source(4, 3);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) {
            const float f = static_cast<float>(y * 4 + x) / 12.0f;
            cpu_destination.set_pixel(x, y, chronon3d::Color{0.1f + f * 0.2f, 0.2f, 0.3f, 0.4f + f * 0.2f});
            cpu_source.set_pixel(x, y, chronon3d::Color{0.05f, 0.1f + f * 0.1f, 0.2f, 0.2f + f * 0.5f});
        }
    }
    auto expected = cpu_destination;
    chronon3d::SoftwareCompositor::composite_layer(
        expected, cpu_source, chronon3d::BlendMode::Normal, std::nullopt,
        chronon3d::CompositeOperator::SourceOver, true);

    const chronon3d::runtime::SurfaceDesc desc{
        4, 3, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    std::vector<float> destination(4 * 3 * 4);
    std::vector<float> source(4 * 3 * 4);
    std::vector<float> actual(destination.size(), 0.0f);
    auto pack = [](const chronon3d::Framebuffer& framebuffer, std::vector<float>& output) {
        std::size_t index = 0;
        for (int y = 0; y < framebuffer.height(); ++y) {
            for (int x = 0; x < framebuffer.width(); ++x) {
                const auto color = framebuffer.get_pixel(x, y);
                output[index++] = color.r;
                output[index++] = color.g;
                output[index++] = color.b;
                output[index++] = color.a;
            }
        }
    };
    pack(cpu_destination, destination);
    pack(cpu_source, source);
    REQUIRE(backend.create_surface(131, desc).ok());
    REQUIRE(backend.create_surface(132, desc).ok());
    REQUIRE(backend.upload_surface(131, desc, destination).ok());
    REQUIRE(backend.upload_surface(132, desc, source).ok());
    REQUIRE(backend.composite_surfaces(
        131, 132, chronon3d::BlendMode::Normal,
        chronon3d::CompositeOperator::SourceOver).ok());
    REQUIRE(backend.download_surface(131, actual).ok());

    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) {
            const auto expected_pixel = expected.get_pixel(x, y);
            const std::size_t index = (static_cast<std::size_t>(y) * 4 + x) * 4;
            CHECK(actual[index + 0] == doctest::Approx(expected_pixel.r).epsilon(1e-5));
            CHECK(actual[index + 1] == doctest::Approx(expected_pixel.g).epsilon(1e-5));
            CHECK(actual[index + 2] == doctest::Approx(expected_pixel.b).epsilon(1e-5));
            CHECK(actual[index + 3] == doctest::Approx(expected_pixel.a).epsilon(1e-5));
        }
    }
}
#endif
