// test_backend_registry_vulkan.cpp — Vulkan device, node dispatch and
// surface upload tests (split from test_backend_registry.cpp).
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
#include <tests/helpers/command_plan_executor.hpp>
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

#ifdef CHRONON3D_ENABLE_VULKAN
TEST_CASE("packed text-run builder packs glyphs and composites via Vulkan") {
    using namespace chronon3d;
    using namespace chronon3d::graph;
    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.services.backend = &backend;
    ctx.services.surface_registry = &surfaces;

    Framebuffer canvas(4, 2);  // transparent
    std::vector<text_run::GpuTextGlyph> glyphs;
    {
        text_run::GpuTextGlyph red;
        red.width = 1;
        red.height = 1;
        red.rgba = {1.0f, 0.0f, 0.0f, 1.0f};  // premultiplied red
        red.dst_x = 1;
        red.dst_y = 0;
        red.opacity = 1.0f;
        glyphs.push_back(std::move(red));
    }
    {
        text_run::GpuTextGlyph green;
        green.width = 1;
        green.height = 1;
        green.rgba = {0.0f, 1.0f, 0.0f, 1.0f};  // premultiplied green
        green.dst_x = 3;
        green.dst_y = 1;
        green.opacity = 0.5f;
        glyphs.push_back(std::move(green));
    }

    const auto r = text_run::draw_packed_text_run(ctx, canvas, glyphs);
    REQUIRE(r.ok());
    CHECK(r.value().items_drawn == 2);

    const auto handle = canvas.surface_handle();
    REQUIRE(handle != runtime::kInvalidRenderSurfaceHandle);
    std::vector<float> output(4 * 2 * 4, 0.0f);
    REQUIRE(backend.download_surface(handle, output).ok());
    const auto pixel = [&](int x, int y) -> const float* {
        return &output[static_cast<std::size_t>(y * 4 + x) * 4];
    };

    // Red glyph (full opacity) landed at (1,0).
    CHECK(pixel(1, 0)[0] == doctest::Approx(1.0f));
    CHECK(pixel(1, 0)[3] == doctest::Approx(1.0f));
    // Green glyph (half opacity) landed at (3,1) premultiplied.
    CHECK(pixel(3, 1)[1] == doctest::Approx(0.5f));
    CHECK(pixel(3, 1)[3] == doctest::Approx(0.5f));
    // No bleed outside the glyph quads.
    CHECK(pixel(0, 0)[3] == doctest::Approx(0.0f));
    CHECK(pixel(2, 1)[3] == doctest::Approx(0.0f));
}

TEST_CASE("cached text-run builds glyphs on both styled cache miss and hit via Vulkan") {
    using namespace chronon3d;
    using namespace chronon3d::graph;

    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    TextRenderResources text_resources;
    runtime::GpuStyledGlyphCache gpu_cache;
    RenderGraphContext ctx;
    ctx.services.backend = &backend;
    ctx.services.surface_registry = &surfaces;
    ctx.services.text_render_resources = &text_resources;
    ctx.services.gpu_text_atlas_cache = &gpu_cache;

    // Pre-populate glyph atlas with an unstyled glyph entry
    auto glyph_img = std::make_shared<BLImage>(4, 4, BL_FORMAT_PRGB32);
    BLImageData img_data{};
    REQUIRE(glyph_img->getData(&img_data) == BL_SUCCESS);
    std::memset(img_data.pixelData, 0xFF, static_cast<std::size_t>(img_data.size.h) * img_data.stride);

    GlyphAtlasEntry entry;
    entry.image = glyph_img;
    entry.x_offset = 0;
    entry.y_offset = 0;
    entry.advance_x = 4.0f;
    text_resources.store_glyph_atlas("test_font", 1, 12, entry);

    TextRunShape shape;
    auto layout = std::make_shared<TextRunLayout>();
    layout->font.font_path = "test_font";
    layout->font_size = 12.0f;

    PlacedGlyph pg{};
    pg.glyph_id = 1;
    pg.bbox_x0 = 0.0f;
    pg.bbox_x1 = 4.0f;
    pg.bbox_y0 = 4.0f;
    pg.bbox_y1 = 0.0f;
    layout->placed.glyphs.push_back(pg);
    layout->units.glyph_to_word.assign(1, 0);
    shape.layout = layout;

    GlyphInstanceState gs{};
    gs.glyph_id = 1;
    gs.scale = {1.0f, 1.0f, 1.0f};
    gs.opacity = 1.0f;
    shape.glyphs.push_back(gs);

    RenderCounters counters{};
    profiling::g_current_counters = &counters;

    // Frame 0: Cold Cache (MISS)
    Framebuffer canvas0(16, 16);
    const auto r0 = text_run::draw_cached_text_run(ctx, canvas0, shape, glm::mat4(1.0f), 1.0f);
    REQUIRE(r0.ok());
    CHECK(counters.gpu_text_styled_cache_misses.load() == 1);
    CHECK(counters.gpu_text_styled_cache_hits.load() == 0);
    CHECK(counters.gpu_text_glyphs_built.load() == 1);

    // Frame 1: Warm Cache (HIT) -> MUST build glyphs even on cache hit!
    Framebuffer canvas1(16, 16);
    const auto r1 = text_run::draw_cached_text_run(ctx, canvas1, shape, glm::mat4(1.0f), 1.0f);
    REQUIRE(r1.ok());
    CHECK(counters.gpu_text_styled_cache_misses.load() == 1);
    CHECK(counters.gpu_text_styled_cache_hits.load() == 1);
    CHECK(counters.gpu_text_glyphs_built.load() == 2);

    profiling::g_current_counters = nullptr;
}
#endif

TEST_CASE("TrackMatteNode dispatches aligned full-frame matte to Vulkan") {
    using namespace chronon3d;
    using namespace chronon3d::graph;
    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.frame_input.width = 2;
    ctx.frame_input.height = 2;
    ctx.services.backend = &backend;
    ctx.services.surface_registry = &surfaces;

    auto target = std::make_shared<Framebuffer>(2, 2);
    auto matte = std::make_shared<Framebuffer>(2, 2);
    target->set_pixel(0, 0, Color{1.0f, 0.5f, 0.25f, 1.0f});
    matte->set_pixel(0, 0, Color{0.0f, 0.0f, 0.0f, 0.25f});
    target->set_pixel(1, 0, Color{0.4f, 0.3f, 0.2f, 0.5f});
    matte->set_pixel(1, 0, Color{0.4f, 0.1f, 0.05f, 0.5f});
    target->set_pixel(0, 1, Color{0.8f, 0.7f, 0.6f, 1.0f});
    matte->set_pixel(0, 1, Color{1.0f, 0.0f, 0.0f, 0.0f});
    target->set_pixel(1, 1, Color{0.1f, 0.2f, 0.3f, 0.25f});
    matte->set_pixel(1, 1, Color{0.2f, 0.3f, 0.4f, 1.0f});

    TrackMatteNode node(TrackMatteType::Alpha, "gpu-matte", {});
    const std::array<FramebufferRef, 2> inputs{target.get(), matte.get()};
    const std::array<std::optional<raster::BBox>, 2> bboxes{
        raster::BBox{0, 0, 2, 2}, raster::BBox{0, 0, 2, 2}};
    const auto result = node.execute(ctx, inputs, bboxes);
    REQUIRE(result.ok());
    REQUIRE(result.value() != nullptr);
    const auto handle = result.value()->surface_handle();
    REQUIRE(handle != runtime::kInvalidRenderSurfaceHandle);
    std::vector<float> output(16, 0.0f);
    REQUIRE(backend.download_surface(handle, output).ok());
    CHECK(output[0] == doctest::Approx(0.25f));
    CHECK(output[1] == doctest::Approx(0.125f));
    CHECK(output[2] == doctest::Approx(0.0625f));
    CHECK(output[3] == doctest::Approx(0.25f));
    CHECK(output[8] == doctest::Approx(0.0f));
    REQUIRE(backend.release_surface(handle).ok());
    CHECK(surfaces.release(handle));
}

TEST_CASE("SourceNode dispatches a solid rect through Vulkan fill_rect_surface") {
    using namespace chronon3d;
    using namespace chronon3d::graph;

    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;

    RenderGraphContext ctx;
    ctx.frame_input.width = 4;
    ctx.frame_input.height = 4;
    ctx.services.backend = &backend;
    ctx.services.surface_registry = &surfaces;

    RenderNode node;
    node.shape = Shape(RectShape{{2.0f, 2.0f}, {}});
    node.color = Color{1.0f, 0.0f, 0.0f, 0.5f};
    node.world_transform = Transform{};

    SourceNode source("rect", node, cache::NodeCacheKey{},
                      std::optional<Mat4>(Mat4(1.0f)),
                      std::optional<f32>(1.0f),
                      static_memory_cache("source"),
                      /*apply_camera_projection=*/false);

    const std::span<const FramebufferRef> no_inputs;
    const std::span<const std::optional<raster::BBox>> no_bboxes;
    const auto result = source.execute(ctx, no_inputs, no_bboxes);
    REQUIRE(result.ok());
    REQUIRE(result.value() != nullptr);

    const auto handle = result.value()->surface_handle();
    REQUIRE(handle != runtime::kInvalidRenderSurfaceHandle);

    std::vector<float> output(4 * 4 * 4, 0.0f);
    REQUIRE(backend.download_surface(handle, output).ok());

    const auto px = [&](int x, int y) -> const float* {
        return &output[static_cast<std::size_t>(y * 4 + x) * 4];
    };
    // Premultiplied red at 0.5 alpha covers [0,2) x [0,2): rgb is scaled by
    // alpha (0.5), matching the software compositor's premultiplied storage.
    CHECK(px(0, 0)[0] == doctest::Approx(0.5f));
    CHECK(px(0, 0)[1] == doctest::Approx(0.0f));
    CHECK(px(0, 0)[3] == doctest::Approx(0.5f));
    CHECK(px(1, 1)[0] == doctest::Approx(0.5f));
    // Pixels outside the rect stay transparent (no safety-padding over-fill).
    CHECK(px(2, 0)[3] == doctest::Approx(0.0f));
    CHECK(px(3, 3)[3] == doctest::Approx(0.0f));

    REQUIRE(backend.release_surface(handle).ok());
    CHECK(surfaces.release(handle));
}

TEST_CASE("Vulkan fill_rect_surface matches the CPU oracle pixel-for-pixel") {
    using namespace chronon3d;
    using namespace chronon3d::graph;
    backends::vulkan::VulkanBackend backend;

    constexpr std::uint32_t width = 6;
    constexpr std::uint32_t height = 5;
    const runtime::SurfaceDesc desc{
        width, height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage, runtime::LifetimeClass::FrameTransient, 0};

    // Non-trivial initial background so the "pixels outside the rect are left
    // untouched" half of the contract is verifiable (not conflated with a
    // transparent clear, as the SourceNode test above necessarily is).
    std::vector<float> background(static_cast<std::size_t>(width) * height * 4, 0.0f);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto i = (static_cast<std::size_t>(y) * width + x) * 4;
            background[i + 0] = static_cast<float>(x) / 10.0f;
            background[i + 1] = static_cast<float>(y) / 10.0f;
            background[i + 2] = 0.125f;
            background[i + 3] = 0.375f;
        }
    }

    // The fill rect is half-open [x0,x1) x [y0,y1), deliberately not touching
    // the edges so the untouched border is exercised on all four sides.
    const std::int32_t x0 = 1, y0 = 1, x1 = 5, y1 = 4;
    // Premultiplied solid color (the surface storage convention).
    const float cr = 0.25f, cg = 0.5f, cb = 0.75f, ca = 0.5f;
    const Color premul{cr * ca, cg * ca, cb * ca, ca};

    // CPU oracle: fill the half-open rect with the premultiplied color,
    // leaving every other pixel exactly as the uploaded background.
    std::vector<float> oracle = background;
    for (std::int32_t y = y0; y < y1; ++y) {
        for (std::int32_t x = x0; x < x1; ++x) {
            const auto i = (static_cast<std::size_t>(y) * width + x) * 4;
            oracle[i + 0] = premul.r;
            oracle[i + 1] = premul.g;
            oracle[i + 2] = premul.b;
            oracle[i + 3] = premul.a;
        }
    }

    const runtime::RenderSurfaceHandle handle = 551;
    REQUIRE(backend.create_surface(handle, desc).ok());
    REQUIRE(backend.upload_surface(handle, desc, background).ok());
    REQUIRE(backend.fill_rect_surface(handle, x0, y0, x1, y1, premul).ok());

    std::vector<float> actual(static_cast<std::size_t>(width) * height * 4, 0.0f);
    REQUIRE(backend.download_surface(handle, actual).ok());

    // Full-buffer parity against the CPU oracle.
    const auto comparison = graph::compare_pixels(oracle, actual);
    INFO("fill_rect_surface parity max_delta=" << comparison.max_delta
         << " mean_delta=" << comparison.mean_delta
         << " mismatched_pixels=" << comparison.mismatched_pixels);
    CHECK(comparison.matched);

    // Spot-check the half-open boundary: inside the rect, outside the rect,
    // and the exclusive right/bottom edges.
    const auto px = [&](std::int32_t x, std::int32_t y) -> const float* {
        return &actual[static_cast<std::size_t>(y * width + x) * 4];
    };
    CHECK(px(x0, y0)[0] == doctest::Approx(premul.r));  // inside
    CHECK(px(x1 - 1, y1 - 1)[3] == doctest::Approx(premul.a));  // inside corner
    CHECK(px(x1, y0)[3] == doctest::Approx(0.375f));  // right edge excluded
    CHECK(px(x0, y1)[3] == doctest::Approx(0.375f));  // bottom edge excluded
    CHECK(px(0, 0)[3] == doctest::Approx(0.375f));    // untouched background

    REQUIRE(backend.release_surface(handle).ok());
}

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

TEST_CASE("EffectStackNode dispatches a full-frame glow through Vulkan surfaces") {
    using namespace chronon3d;
    using namespace chronon3d::graph;

    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;

    RenderGraphContext ctx;
    ctx.frame_input.width = 8;
    ctx.frame_input.height = 8;
    ctx.services.backend = &backend;
    ctx.services.surface_registry = &surfaces;

    auto source = std::make_shared<Framebuffer>(8, 8);
    source->clear(Color::transparent());
    source->set_pixel(4, 4, Color{1.0f, 0.5f, 0.25f, 1.0f});

    GlowParams params;
    params.radius = 2.0f;
    params.intensity = 0.5f;
    params.color = Color{0.25f, 0.5f, 1.0f, 1.0f};
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

TEST_CASE("EffectStackNode dispatches a full-frame tint through Vulkan surfaces") {
    using namespace chronon3d;
    using namespace chronon3d::graph;

    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.frame_input.width = 2;
    ctx.frame_input.height = 2;
    ctx.services.backend = &backend;
    ctx.services.surface_registry = &surfaces;

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
    REQUIRE(backend.blur_surface(402, 401, 1.5f, true).ok());
    REQUIRE(backend.blur_surface(403, 402, 1.5f, false).ok());
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
    REQUIRE(backend.blur_surface(523, 522, 1.5f, true).ok());
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
