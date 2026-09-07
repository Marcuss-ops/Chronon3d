// test_backend_registry_vulkan.cpp — Vulkan device, node dispatch and
// surface upload tests (split from test_backend_registry.cpp).
//
// Split parts (each ≤ 400 LOC) keep the Vulkan-enabled coverage compiled in
// exactly the same configurations as this origin file:
//   - test_backend_registry_vulkan_text_run.cpp    (packed/cached text-run)
//   - test_backend_registry_vulkan_dispatch.cpp    (TrackMatte/SourceNode ops)
//   - test_backend_registry_vulkan_surfaces.cpp    (matte/effect/composite)
//   - test_backend_registry_vulkan_transforms.cpp  (upload/transform/blur/batch)
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
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <blend2d.h>
#endif

#include "../../src/render_graph/nodes/text_run/gpu_text_run.hpp"

namespace {

class StubBackend final : public chronon3d::graph::RenderBackend {
public:
    void apply_per_pixel_dof(chronon3d::Framebuffer&, std::span<const float>,
                             const chronon3d::DepthOfFieldSettings&,
                             const chronon3d::LensModel&,
                             const std::optional<chronon3d::raster::BBox>&) override {}
    chronon3d::graph::RenderOpResult draw_node(
        chronon3d::Framebuffer&, const chronon3d::RenderNode&,
        const chronon3d::RenderState&, const chronon3d::Camera&, int, int) override {
        return chronon3d::graph::RenderOpResult{chronon3d::graph::RenderOpOutcome{}};
    }
    void apply_effect_stack(chronon3d::Framebuffer&, const chronon3d::EffectStack&,
                            const chronon3d::effects::EffectExecutionContext&) override {}
    void composite_layer(chronon3d::Framebuffer&, const chronon3d::Framebuffer&,
                         chronon3d::BlendMode,
                         const std::optional<chronon3d::raster::BBox>&,
                         chronon3d::CompositeOperator) override {}
    void apply_blur(chronon3d::Framebuffer&, float,
                    const std::optional<chronon3d::raster::BBox>&) override {}
};

} // namespace
#ifdef CHRONON3D_ENABLE_VULKAN
TEST_CASE("Vulkan backend creates a persistent headless device") {
    auto backend = chronon3d::backends::vulkan::make_vulkan_backend();
    REQUIRE(backend != nullptr);
    const auto* vulkan = dynamic_cast<chronon3d::backends::vulkan::VulkanBackend*>(backend.get());
    REQUIRE(vulkan != nullptr);
    const auto device_stats = vulkan->stats();
    CHECK(device_stats.discrete_gpu);
    CHECK(device_stats.device_name.find("RTX A4000") != std::string::npos);
    CHECK(vulkan->kernel_registry().size() == 12);
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::Composite));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::Transform));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::AffineTransform));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::Blur));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::ColorAdjust));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::Matte));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::TextRun));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::FillRect));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::LayerBatch));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::TextBatch));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::TextTileBin));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::TextTileRaster));
}

TEST_CASE("Vulkan alpha and luma matte match the CPU coverage formulas") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> target{
        1.0f, 0.5f, 0.25f, 1.0f,
        0.4f, 0.3f, 0.2f, 0.5f,
        0.8f, 0.7f, 0.6f, 1.0f,
        0.1f, 0.2f, 0.3f, 0.25f};
    const std::vector<float> matte{
        0.0f, 0.0f, 0.0f, 0.25f,
        0.4f, 0.1f, 0.05f, 0.5f,
        1.0f, 0.0f, 0.0f, 0.0f,
        0.2f, 0.3f, 0.4f, 1.0f};
    std::vector<float> output(16, 0.0f);
    REQUIRE(backend.create_surface(431, desc).ok());
    REQUIRE(backend.create_surface(432, desc).ok());
    REQUIRE(backend.create_surface(433, desc).ok());
    REQUIRE(backend.upload_surface(432, desc, target).ok());
    REQUIRE(backend.upload_surface(433, desc, matte).ok());
    REQUIRE(backend.matte_surface(431, 432, 433, false, false).ok());
    REQUIRE(backend.download_surface(431, output).ok());
    CHECK(output[0] == doctest::Approx(0.25f));
    CHECK(output[1] == doctest::Approx(0.125f));
    CHECK(output[2] == doctest::Approx(0.0625f));
    CHECK(output[3] == doctest::Approx(0.25f));

    REQUIRE(backend.matte_surface(431, 432, 433, true, false).ok());
    REQUIRE(backend.download_surface(431, output).ok());
    const float luma = 0.4f * 0.2126f + 0.1f * 0.7152f + 0.05f * 0.0722f;
    CHECK(output[4] == doctest::Approx(0.4f * luma).epsilon(1e-5));
    CHECK(output[7] == doctest::Approx(0.5f * luma).epsilon(1e-5));
}

TEST_CASE("Vulkan text-run kernel samples a packed glyph atlas into the canvas") {
    using namespace chronon3d;
    backends::vulkan::VulkanBackend backend;

    // 2x1 packed glyph atlas holding two 1x1 glyph quads (premultiplied):
    //   atlas (0,0) = solid red, atlas (1,0) = solid green.
    const runtime::SurfaceDesc atlas_desc{
        2, 1, runtime::PixelFormat::Rgba32Float, runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::JobPersistent, 0};
    const runtime::SurfaceDesc canvas_desc{
        4, 2, runtime::PixelFormat::Rgba32Float, runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> atlas{
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f};
    std::vector<float> empty_canvas(4 * 2 * 4, 0.0f);

    REQUIRE(backend.create_surface(601, canvas_desc).ok());
    REQUIRE(backend.create_surface(602, atlas_desc).ok());
    REQUIRE(backend.upload_surface(602, atlas_desc, atlas).ok());
    REQUIRE(backend.upload_surface(601, canvas_desc, empty_canvas).ok());

    // Red glyph at canvas (1,0) full opacity; green glyph at (3,1) half opacity.
    const std::vector<runtime::GlyphInstance> glyphs{
        runtime::GlyphInstance{1, 0, 0, 0, 1, 1, 1.0f, 0.0f},
        runtime::GlyphInstance{3, 1, 1, 0, 1, 1, 0.5f, 0.0f}};
    REQUIRE(backend.draw_text_run_surface(601, 602, glyphs).ok());

    std::vector<float> output(4 * 2 * 4, 0.0f);
    REQUIRE(backend.download_surface(601, output).ok());
    const auto pixel = [&](int x, int y) -> const float* {
        return &output[static_cast<std::size_t>(y * 4 + x) * 4];
    };

    // Glyph A (red, full opacity) landed at (1,0).
    CHECK(pixel(1, 0)[0] == doctest::Approx(1.0f));
    CHECK(pixel(1, 0)[1] == doctest::Approx(0.0f));
    CHECK(pixel(1, 0)[3] == doctest::Approx(1.0f));
    // Glyph B (green, half opacity) landed at (3,1) premultiplied.
    CHECK(pixel(3, 1)[0] == doctest::Approx(0.0f));
    CHECK(pixel(3, 1)[1] == doctest::Approx(0.5f));
    CHECK(pixel(3, 1)[3] == doctest::Approx(0.5f));
    // No bleed outside the glyph quads.
    CHECK(pixel(0, 0)[3] == doctest::Approx(0.0f));
    CHECK(pixel(2, 0)[3] == doctest::Approx(0.0f));
    CHECK(pixel(3, 0)[3] == doctest::Approx(0.0f));
    CHECK(pixel(0, 1)[3] == doctest::Approx(0.0f));
    CHECK(pixel(2, 1)[3] == doctest::Approx(0.0f));
}

TEST_CASE("packed text-run builder reports UnsupportedCapability without a backend") {
    using namespace chronon3d;
    using namespace chronon3d::graph;
    RenderGraphContext ctx;  // no backend, no surface registry
    Framebuffer canvas(4, 2);
    text_run::GpuTextGlyph g;
    g.width = 1;
    g.height = 1;
    g.rgba = {1.0f, 0.0f, 0.0f, 1.0f};
    std::vector<text_run::GpuTextGlyph> glyphs;
    glyphs.push_back(std::move(g));
    const auto r = text_run::draw_packed_text_run(ctx, canvas, glyphs);
    REQUIRE(!r.ok());
    CHECK(r.error().code == RenderBackendErrorCode::UnsupportedCapability);
}

TEST_CASE("packed text-run builder validates input before touching the device") {
    using namespace chronon3d;
    using namespace chronon3d::graph;
    RenderGraphContext ctx;
    StubBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    ctx.services.backend = &backend;
    ctx.services.surface_registry = &surfaces;
    Framebuffer canvas(4, 2);

    // Empty input is a valid no-op success (outcome 0).
    {
        const auto r = text_run::draw_packed_text_run(ctx, canvas, {});
        REQUIRE(r.ok());
        CHECK(r.value().items_drawn == 0);
    }
    // A glyph whose rgba buffer does not match width*height*4 is rejected
    // before any surface is created (so no device call is attempted).
    {
        text_run::GpuTextGlyph bad;
        bad.width = 1;
        bad.height = 1;
        bad.rgba = {1.0f};  // too short
        std::vector<text_run::GpuTextGlyph> glyphs;
        glyphs.push_back(std::move(bad));
        const auto r = text_run::draw_packed_text_run(ctx, canvas, glyphs);
        REQUIRE(!r.ok());
        CHECK(r.error().code == RenderBackendErrorCode::InvalidInput);
    }
}
#endif
