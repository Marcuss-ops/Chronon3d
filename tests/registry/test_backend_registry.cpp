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

TEST_CASE("backend registry rejects duplicates and resolves explicit preferences") {
    using namespace chronon3d::graph;
    BackendRegistry registry;
    const auto software_factory = [] { return std::make_unique<StubBackend>(); };

    CHECK(registry.register_backend(BackendType::Software,
                                    BackendCapabilities{true, false, false, 4096, 4096, 0},
                                    software_factory));
    CHECK_FALSE(registry.register_backend(BackendType::Software,
                                          BackendCapabilities{}, software_factory));
    CHECK(registry.contains(BackendType::Software));
    CHECK_FALSE(registry.contains(BackendType::Vulkan));

    BackendResolver resolver(registry);
    auto explicit_cpu = resolver.resolve(BackendPreference::Software);
    REQUIRE(explicit_cpu.ok());
    CHECK(dynamic_cast<StubBackend*>(explicit_cpu.value().get()) != nullptr);

    auto strict_gpu = resolver.resolve(BackendPreference::GPU);
    CHECK_FALSE(strict_gpu.ok());
    CHECK(strict_gpu.error().code == BackendResolveErrorCode::NotRegistered);
}

TEST_CASE("auto prefers Vulkan and falls back to software") {
    using namespace chronon3d::graph;
    BackendRegistry registry;
    CHECK(registry.register_backend(BackendType::Software, BackendCapabilities{true},
                                    [] { return std::make_unique<StubBackend>(); }));

    BackendResolver resolver(registry);
    auto auto_backend = resolver.resolve(BackendPreference::Auto);
    REQUIRE(auto_backend.ok());
    CHECK(dynamic_cast<StubBackend*>(auto_backend.value().get()) != nullptr);
}

TEST_CASE("requirements are checked before factory invocation") {
    using namespace chronon3d::graph;
    BackendRegistry registry;
    CHECK(registry.register_backend(BackendType::Vulkan,
                                    BackendCapabilities{true, true, false, 8192, 8192, 1ull << 30},
                                    [] { return std::make_unique<StubBackend>(); }));
    BackendResolver resolver(registry);

    auto missing_compute = resolver.resolve(BackendPreference::GPU,
                                            BackendRequirements{true, true, true});
    CHECK_FALSE(missing_compute.ok());
    CHECK(missing_compute.error().code == BackendResolveErrorCode::NoMatchingBackend);

    auto supported = resolver.resolve(BackendPreference::GPU,
                                      BackendRequirements{true, true, false, 1920, 1080});
    CHECK(supported.ok());
}

TEST_CASE("factory exceptions become structured resolution failures") {
    using namespace chronon3d::graph;
    BackendRegistry registry;
    CHECK(registry.register_backend(BackendType::Vulkan, BackendCapabilities{true}, []() -> std::unique_ptr<RenderBackend> {
        throw std::runtime_error("device unavailable");
    }));

    const auto result = BackendResolver{registry}.resolve(BackendPreference::GPU);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == BackendResolveErrorCode::FactoryFailed);
    CHECK(result.error().message.find("device unavailable") != std::string::npos);
}

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

TEST_CASE("Vulkan backend composites premultiplied source-over pixels") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    chronon3d::Framebuffer destination(2, 2);
    chronon3d::Framebuffer source(2, 2);
    destination.clear(chronon3d::Color{0.1f, 0.2f, 0.3f, 0.8f});
    source.clear(chronon3d::Color{0.5f, 0.25f, 0.125f, 0.5f});

    backend.composite_layer(destination, source, chronon3d::BlendMode::Normal,
                            std::nullopt, chronon3d::CompositeOperator::SourceOver);

    const auto pixel = destination.get_pixel(0, 0);
    CHECK(pixel.r == doctest::Approx(0.5f + 0.1f * 0.5f));
    CHECK(pixel.g == doctest::Approx(0.25f + 0.2f * 0.5f));
    CHECK(pixel.b == doctest::Approx(0.125f + 0.3f * 0.5f));
    CHECK(pixel.a == doctest::Approx(0.5f + 0.8f * 0.5f));

    backend.composite_layer(destination, source, chronon3d::BlendMode::Normal,
                            std::nullopt, chronon3d::CompositeOperator::SourceOver);
    const auto reused_pixel = destination.get_pixel(0, 0);
    CHECK(reused_pixel.r == doctest::Approx(0.5f + (0.5f + 0.1f * 0.5f) * 0.5f));
    CHECK(reused_pixel.a == doctest::Approx(0.5f + (0.5f + 0.8f * 0.5f) * 0.5f));
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

TEST_CASE("Vulkan frame batch descriptor allocator grows past the first chunk") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        4, 4, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    std::vector<float> source(4 * 4 * 4, 0.0f);
    const auto center = (static_cast<std::size_t>(2) * 4 + 2) * 4;
    source[center + 0] = 0.9f;
    source[center + 3] = 1.0f;
    std::vector<float> output(source.size(), 0.0f);

    REQUIRE(backend.create_surface(531, desc).ok());
    REQUIRE(backend.create_surface(532, desc).ok());
    REQUIRE(backend.upload_surface(531, desc, source).ok());

    // 100 recorded passes need 200 storage-image descriptors, exceeding the
    // first 64-set chunk (192 descriptors): the allocator must grow a second
    // chunk (128 sets) while the whole frame still submits exactly once.
    const auto submissions_before = backend.stats().submissions;
    backend.begin_frame_batch();
    for (int i = 0; i < 100; ++i) {
        REQUIRE(backend.transform_surface(532, 531, 0, 0, 1.0f).ok());
    }
    backend.end_frame_batch();
    CHECK(backend.stats().submissions == submissions_before + 1);

    REQUIRE(backend.download_surface(532, output).ok());
    CHECK(output[center + 0] == doctest::Approx(0.9f).epsilon(1e-3f));
}

TEST_CASE("Vulkan plan-driven batch synchronizes through the BarrierPlan mapper") {
    using namespace chronon3d::runtime;
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        4, 4, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    std::vector<float> source(4 * 4 * 4, 0.0f);
    const auto center = (static_cast<std::size_t>(2) * 4 + 2) * 4;
    source[center + 0] = 0.7f;
    source[center + 3] = 1.0f;
    std::vector<float> output(source.size(), 0.0f);

    REQUIRE(backend.create_surface(541, desc).ok());
    REQUIRE(backend.create_surface(542, desc).ok());
    REQUIRE(backend.create_surface(543, desc).ok());
    REQUIRE(backend.upload_surface(541, desc, source).ok());

    // Compile the same dependency chain as a CommandPlan so the backend
    // synchronizes through the BarrierPlan mapper (precise compute-stage
    // barriers) instead of the conservative per-pass fallback.
    const ResourceDesc resource_desc{4, 4, PixelFormat::Rgba32Float,
                                     ResourceUsage::Storage, 4 * 4 * 4 * sizeof(float)};
    GpuCommandPlanner planner;
    planner.declare_surface(541, resource_desc);
    planner.declare_surface(542, resource_desc);
    planner.declare_surface(543, resource_desc);
    planner.transform(TransformPass{542, 541, 0, 0, 1.0f});
    planner.blur(BlurPass{543, 542, 1.5f, 1});
    planner.composite(CompositePass{543, 541, 0});
    const auto plan = planner.build();
    REQUIRE(plan.passes.size() == 3);
    CHECK(plan.barriers.size() >= 4);

    const auto submissions_before = backend.stats().submissions;
    backend.begin_plan_batch(plan);
    REQUIRE(backend.transform_surface(542, 541, 0, 0, 1.0f).ok());
    REQUIRE(backend.blur_surface(543, 542, 1.5f, true).ok());
    REQUIRE(backend.composite_surfaces(
        543, 541, chronon3d::BlendMode::Normal,
        chronon3d::CompositeOperator::SourceOver).ok());
    backend.end_frame_batch();
    // Three plan passes still coalesce into exactly one queue submission.
    CHECK(backend.stats().submissions == submissions_before + 1);

    REQUIRE(backend.download_surface(543, output).ok());
    // The write→read chain (transform writes 542, blur reads 542, composite
    // overwrites 543) produced a valid result through the plan barriers.
    CHECK(output[center + 0] > 0.0f);
    CHECK(output[center + 3] > 0.0f);
}

TEST_CASE("command plan executor dispatches passes through the canonical backend API") {
    using namespace chronon3d::runtime;
    chronon3d::backends::vulkan::VulkanBackend backend;
    RenderSurfaceRegistry registry;
    const SurfaceDesc surface_desc{4, 4, PixelFormat::Rgba32Float,
                                   ResourceUsage::Storage,
                                   LifetimeClass::FrameTransient, 0};
    const auto input = registry.create(surface_desc);
    const auto scratch = registry.create(surface_desc);
    const auto output = registry.create(surface_desc);
    REQUIRE(input != kInvalidRenderSurfaceHandle);
    REQUIRE(scratch != kInvalidRenderSurfaceHandle);
    REQUIRE(output != kInvalidRenderSurfaceHandle);
    REQUIRE(backend.create_surface(input, surface_desc).ok());
    REQUIRE(backend.create_surface(scratch, surface_desc).ok());
    REQUIRE(backend.create_surface(output, surface_desc).ok());

    std::vector<float> source(4 * 4 * 4, 0.0f);
    const auto center = (static_cast<std::size_t>(2) * 4 + 2) * 4;
    source[center + 0] = 0.6f;
    source[center + 3] = 1.0f;
    REQUIRE(backend.upload_surface(input, surface_desc, source).ok());

    const ResourceDesc resource_desc{4, 4, PixelFormat::Rgba32Float,
                                     ResourceUsage::Storage,
                                     4 * 4 * 4 * sizeof(float)};
    GpuCommandPlanner planner;
    planner.declare_surface(input, resource_desc);
    planner.declare_surface(scratch, resource_desc);
    planner.declare_surface(output, resource_desc);
    planner.transform(TransformPass{scratch, input, 0, 0, 1.0f});
    planner.blur(BlurPass{output, scratch, 1.5f, 1});
    planner.composite(CompositePass{output, input, 0});
    const auto plan = planner.build();

    const auto submissions_before = backend.stats().submissions;
    REQUIRE(execute_command_plan(backend, registry, plan));
    // The executor opens + closes one frame batch: every plan pass coalesces
    // into a single queue submission.
    CHECK(backend.stats().submissions == submissions_before + 1);

    std::vector<float> result(4 * 4 * 4, 0.0f);
    REQUIRE(backend.download_surface(output, result).ok());
    CHECK(result[center + 0] > 0.0f);
    CHECK(result[center + 3] > 0.0f);
}

TEST_CASE("Vulkan backend exports gpu_submissions and passes_executed telemetry counters") {
    using namespace chronon3d::runtime;
    chronon3d::backends::vulkan::VulkanBackend backend;
    RenderSurfaceRegistry registry;
    const SurfaceDesc surface_desc{4, 4, PixelFormat::Rgba32Float,
                                   ResourceUsage::Storage,
                                   LifetimeClass::FrameTransient, 0};
    const auto input = registry.create(surface_desc);
    const auto scratch = registry.create(surface_desc);
    const auto output = registry.create(surface_desc);
    REQUIRE(input != kInvalidRenderSurfaceHandle);
    REQUIRE(scratch != kInvalidRenderSurfaceHandle);
    REQUIRE(output != kInvalidRenderSurfaceHandle);
    for (const auto handle : {input, scratch, output}) {
        REQUIRE(backend.create_surface(handle, surface_desc).ok());
    }

    std::vector<float> source(4 * 4 * 4, 0.0f);
    source[(static_cast<std::size_t>(1) * 4 + 1) * 4 + 0] = 0.5f;
    source[(static_cast<std::size_t>(1) * 4 + 1) * 4 + 3] = 1.0f;
    REQUIRE(backend.upload_surface(input, surface_desc, source).ok());

    const ResourceDesc resource_desc{4, 4, PixelFormat::Rgba32Float,
                                     ResourceUsage::Storage,
                                     4 * 4 * 4 * sizeof(float)};
    GpuCommandPlanner planner;
    planner.declare_surface(input, resource_desc);
    planner.declare_surface(scratch, resource_desc);
    planner.declare_surface(output, resource_desc);
    planner.transform(TransformPass{scratch, input, 0, 0, 1.0f});
    planner.blur(BlurPass{output, scratch, 1.5f, 1});
    const auto plan = planner.build();
    REQUIRE(plan.passes.size() == 2);

    const auto before = backend.stats();
    REQUIRE(execute_command_plan(backend, registry, plan));
    const auto after = backend.stats();
    // One vkQueueSubmit for the 2-pass plan, and exactly two executed passes
    // (independent of how many submits the passes coalesced into).
    CHECK(after.submissions == before.submissions + 1);
    CHECK(after.passes_executed == before.passes_executed + 2);

    // The export mirrors the live counters as name/value pairs that the
    // telemetry render_counters table consumes.
    std::vector<std::pair<std::string, std::uint64_t>> exported;
    backend.export_gpu_telemetry_counters(exported);
    bool found_submissions = false;
    bool found_passes = false;
    for (const auto& [name, value] : exported) {
        if (name == "gpu_submissions") {
            CHECK(value == after.submissions);
            found_submissions = true;
        } else if (name == "passes_executed") {
            CHECK(value == after.passes_executed);
            found_passes = true;
        }
    }
    CHECK(found_submissions);
    CHECK(found_passes);
}

TEST_CASE("checkpoint: Background-Transform-Blur-Composite-ColorAdjust is one submission") {
    using namespace chronon3d::runtime;
    chronon3d::backends::vulkan::VulkanBackend backend;
    RenderSurfaceRegistry registry;
    const SurfaceDesc surface_desc{4, 4, PixelFormat::Rgba32Float,
                                   ResourceUsage::Storage,
                                   LifetimeClass::FrameTransient, 0};
    const auto background = registry.create(surface_desc);
    const auto text = registry.create(surface_desc);
    const auto transformed = registry.create(surface_desc);
    const auto blurred = registry.create(surface_desc);
    const auto output = registry.create(surface_desc);
    REQUIRE(background != kInvalidRenderSurfaceHandle);
    REQUIRE(text != kInvalidRenderSurfaceHandle);
    REQUIRE(transformed != kInvalidRenderSurfaceHandle);
    REQUIRE(blurred != kInvalidRenderSurfaceHandle);
    REQUIRE(output != kInvalidRenderSurfaceHandle);
    for (const auto handle : {background, text, transformed, blurred, output}) {
        REQUIRE(backend.create_surface(handle, surface_desc).ok());
    }

    std::vector<float> bg(4 * 4 * 4, 0.0f);
    const auto center = (static_cast<std::size_t>(2) * 4 + 2) * 4;
    bg[center + 0] = 0.8f;
    bg[center + 3] = 1.0f;
    std::vector<float> txt(4 * 4 * 4, 0.0f);
    const auto corner = 0u;
    txt[corner + 0] = 0.9f;
    txt[corner + 3] = 1.0f;
    REQUIRE(backend.upload_surface(background, surface_desc, bg).ok());
    REQUIRE(backend.upload_surface(text, surface_desc, txt).ok());

    const ResourceDesc resource_desc{4, 4, PixelFormat::Rgba32Float,
                                     ResourceUsage::Storage,
                                     4 * 4 * 4 * sizeof(float)};
    GpuCommandPlanner planner;
    planner.declare_surface(background, resource_desc);
    planner.declare_surface(text, resource_desc);
    planner.declare_surface(transformed, resource_desc);
    planner.declare_surface(blurred, resource_desc);
    planner.declare_surface(output, resource_desc);
    planner.transform(TransformPass{transformed, background, 0, 0, 1.0f});
    planner.blur(BlurPass{blurred, transformed, 1.5f, 1});
    planner.composite(CompositePass{blurred, text, 0});
    planner.color_adjust(ColorAdjustPass{output, blurred, 0.0f, 1.0f, 0.0f,
                                         {1.0f, 1.0f, 1.0f, 1.0f}});
    const auto plan = planner.build();
    REQUIRE(plan.passes.size() == 4);
    // One barrier transition per pass boundary (≥4 for the 4-pass chain).
    CHECK(plan.barriers.size() >= 4);

    // Aliasing proof: the 5 logical surfaces never overlap in pairs whose
    // lifetimes are disjoint, so the planner must back them with fewer
    // physical slots than logical surfaces.
    std::unordered_set<std::size_t> physical_slots;
    for (const auto& allocation : plan.resources.allocations) {
        if (allocation.physical_slot != std::numeric_limits<std::size_t>::max()) {
            physical_slots.insert(allocation.physical_slot);
        }
    }
    CHECK(physical_slots.size() < 5);

    const auto submissions_before = backend.stats().submissions;
    REQUIRE(execute_command_plan(backend, registry, plan));
    // The whole 4-pass scene coalesces into exactly one queue submission.
    CHECK(backend.stats().submissions == submissions_before + 1);

    std::vector<float> result(4 * 4 * 4, 0.0f);
    REQUIRE(backend.download_surface(output, result).ok());
    // Background center survived transform→blur→color adjust; the text
    // corner survived the composite.
    CHECK(result[center + 0] > 0.0f);
    CHECK(result[center + 3] > 0.0f);
    CHECK(result[corner + 0] > 0.0f);
}

TEST_CASE("minimal background + 1 overlay render produces a pixel-correct frame") {
    using namespace chronon3d::runtime;
    chronon3d::backends::vulkan::VulkanBackend backend;
    RenderSurfaceRegistry registry;

    constexpr std::uint32_t kWidth = 4;
    constexpr std::uint32_t kHeight = 4;
    constexpr std::size_t kPixelCount = kWidth * kHeight;
    constexpr std::size_t kFloatCount = kPixelCount * 4;
    const auto at = [](std::size_t x, std::size_t y) {
        return (y * kWidth + x) * 4;
    };

    const SurfaceDesc surface_desc{kWidth, kHeight, PixelFormat::Rgba32Float,
                                   ResourceUsage::Storage,
                                   LifetimeClass::FrameTransient, 0};
    const auto background = registry.create(surface_desc);
    const auto overlay = registry.create(surface_desc);
    const auto output = registry.create(surface_desc);
    REQUIRE(background != kInvalidRenderSurfaceHandle);
    REQUIRE(overlay != kInvalidRenderSurfaceHandle);
    REQUIRE(output != kInvalidRenderSurfaceHandle);
    for (const auto handle : {background, overlay, output}) {
        REQUIRE(backend.create_surface(handle, surface_desc).ok());
    }

    // Background: an opaque per-pixel varying pattern so any cross-pixel
    // bleed or mis-copy is caught by the full-frame check below.
    std::vector<float> background_px(kFloatCount, 0.0f);
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            const float f = static_cast<float>(y * kWidth + x) /
                            static_cast<float>(kPixelCount - 1);
            background_px[at(x, y) + 0] = 0.1f + f * 0.5f;
            background_px[at(x, y) + 1] = 0.2f + f * 0.3f;
            background_px[at(x, y) + 2] = 0.3f + f * 0.2f;
            background_px[at(x, y) + 3] = 1.0f;
        }
    }
    // Overlay: one opaque premultiplied red pixel at the centre, transparent
    // everywhere else.
    std::vector<float> overlay_px(kFloatCount, 0.0f);
    overlay_px[at(2, 2) + 0] = 1.0f;  // R
    overlay_px[at(2, 2) + 3] = 1.0f;  // A (premultiplied: a == 1)
    REQUIRE(backend.upload_surface(background, surface_desc, background_px).ok());
    REQUIRE(backend.upload_surface(overlay, surface_desc, overlay_px).ok());

    // Minimal render: copy the background into the output, then composite the
    // single overlay on top (source-over).
    const ResourceDesc resource_desc{kWidth, kHeight, PixelFormat::Rgba32Float,
                                     ResourceUsage::Storage,
                                     kFloatCount * sizeof(float)};
    GpuCommandPlanner planner;
    planner.declare_surface(background, resource_desc);
    planner.declare_surface(overlay, resource_desc);
    planner.declare_surface(output, resource_desc);
    planner.transform(TransformPass{output, background, 0, 0, 1.0f});
    planner.composite(CompositePass{output, overlay, 0});
    const auto plan = planner.build();
    REQUIRE(plan.passes.size() == 2);

    REQUIRE(execute_command_plan(backend, registry, plan));

    std::vector<float> actual(kFloatCount, 0.0f);
    REQUIRE(backend.download_surface(output, actual).ok());

    // Pixel-correct: every pixel is the background pattern, except the centre
    // where the opaque overlay wins outright (source-over: src + dst*(1-src.a)).
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            const bool centre = (x == 2 && y == 2);
            const std::size_t index = at(x, y);
            for (int c = 0; c < 4; ++c) {
                const float expected = centre ? ((c == 0 || c == 3) ? 1.0f : 0.0f)
                                              : background_px[index + c];
                CHECK(actual[index + c] ==
                      doctest::Approx(expected).epsilon(1e-5));
            }
        }
    }
}

TEST_CASE("medium CommandPlan render certifies a correct frame through pass barriers") {
    using namespace chronon3d::runtime;
    chronon3d::backends::vulkan::VulkanBackend backend;
    RenderSurfaceRegistry registry;

    constexpr std::uint32_t kWidth = 6;
    constexpr std::uint32_t kHeight = 6;
    constexpr std::size_t kPixelCount = kWidth * kHeight;
    constexpr std::size_t kFloatCount = kPixelCount * 4;
    const auto at = [](std::size_t x, std::size_t y) {
        return (y * kWidth + x) * 4;
    };

    const SurfaceDesc surface_desc{kWidth, kHeight, PixelFormat::Rgba32Float,
                                   ResourceUsage::Storage,
                                   LifetimeClass::FrameTransient, 0};
    const auto overlay = registry.create(surface_desc);
    const auto transformed = registry.create(surface_desc);
    const auto blurred = registry.create(surface_desc);
    const auto background = registry.create(surface_desc);
    const auto output = registry.create(surface_desc);
    REQUIRE(overlay != kInvalidRenderSurfaceHandle);
    REQUIRE(transformed != kInvalidRenderSurfaceHandle);
    REQUIRE(blurred != kInvalidRenderSurfaceHandle);
    REQUIRE(background != kInvalidRenderSurfaceHandle);
    REQUIRE(output != kInvalidRenderSurfaceHandle);
    for (const auto handle : {overlay, transformed, blurred, background, output}) {
        REQUIRE(backend.create_surface(handle, surface_desc).ok());
    }

    // Inputs: one opaque premultiplied red overlay pixel at (1,3) over an
    // opaque constant background.  Distinct values keep every pass's
    // contribution individually checkable.
    std::vector<float> overlay_px(kFloatCount, 0.0f);
    overlay_px[at(1, 3) + 0] = 1.0f;  // R (premultiplied: alpha == 1)
    overlay_px[at(1, 3) + 3] = 1.0f;  // A
    std::vector<float> background_px(kFloatCount, 0.0f);
    for (std::size_t p = 0; p < kPixelCount; ++p) {
        background_px[p * 4 + 0] = 0.2f;
        background_px[p * 4 + 1] = 0.3f;
        background_px[p * 4 + 2] = 0.4f;
        background_px[p * 4 + 3] = 1.0f;
    }
    REQUIRE(backend.upload_surface(overlay, surface_desc, overlay_px).ok());
    REQUIRE(backend.upload_surface(background, surface_desc, background_px).ok());

    // Medium scene: transform → blur → composite → color adjust.
    const ResourceDesc resource_desc{kWidth, kHeight, PixelFormat::Rgba32Float,
                                     ResourceUsage::Storage,
                                     kFloatCount * sizeof(float)};
    GpuCommandPlanner planner;
    planner.declare_surface(overlay, resource_desc);
    planner.declare_surface(transformed, resource_desc);
    planner.declare_surface(blurred, resource_desc);
    planner.declare_surface(background, resource_desc);
    planner.declare_surface(output, resource_desc);
    planner.transform(TransformPass{transformed, overlay, 1, 0, 1.0f});
    planner.blur(BlurPass{blurred, transformed, 0.5f, 1});
    planner.composite(CompositePass{background, blurred, 0});
    planner.color_adjust(ColorAdjustPass{output, background, 0.1f, 1.2f, 0.25f,
                                         {0.2f, 0.8f, 0.4f, 1.0f}});
    const auto plan = planner.build();
    REQUIRE(plan.passes.size() == 4);
    // One Read transition per sampled source + one Write per destination
    // = 2 transitions × 4 passes.
    CHECK(plan.barriers.size() == 8);

    // Lifetime-disjoint aliasing: overlay[0,0]/blurred[1,2]/output[3,3]
    // share one physical slot and transformed[0,1]/background[2,3] share the
    // other.  This is the write-after-read aliasing the barriers must order.
    std::unordered_set<std::size_t> physical_slots;
    for (const auto& allocation : plan.resources.allocations) {
        if (allocation.physical_slot != std::numeric_limits<std::size_t>::max()) {
            physical_slots.insert(allocation.physical_slot);
        }
    }
    CHECK(physical_slots.size() == 2);

    // ── CPU oracle mirroring the exact compute-kernel math ────────────────
    // transform.comp: out(dst) = src(dst - offset) * opacity, else 0.
    std::vector<float> transformed_px(kFloatCount, 0.0f);
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            const std::int32_t sx = static_cast<std::int32_t>(x) - 1;
            const std::int32_t sy = static_cast<std::int32_t>(y);
            if (sx >= 0 && sy >= 0 &&
                sx < static_cast<std::int32_t>(kWidth) &&
                sy < static_cast<std::int32_t>(kHeight)) {
                const std::size_t src = (static_cast<std::size_t>(sy) * kWidth +
                                         static_cast<std::size_t>(sx)) * 4;
                for (int c = 0; c < 4; ++c) {
                    transformed_px[at(x, y) + c] = overlay_px[src + c] * 1.0f;
                }
            }
        }
    }

    // blur.comp (horizontal): gaussian with sigma = max(radius, 0.5).
    constexpr float kRadius = 0.5f;
    const float sigma = std::max(kRadius, 0.5f);
    const int extent = std::clamp(static_cast<int>(std::ceil(sigma * 2.0f)), 1, 32);
    std::vector<float> blurred_px(kFloatCount, 0.0f);
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float wsum = 0.0f;
            for (int off = -extent; off <= extent; ++off) {
                const std::int32_t sx = std::clamp(
                    static_cast<std::int32_t>(x) + off, 0,
                    static_cast<std::int32_t>(kWidth) - 1);
                const float dist = static_cast<float>(off);
                const float weight = static_cast<float>(
                    std::exp(-0.5 * dist * dist / (sigma * sigma)));
                const std::size_t sample = (static_cast<std::size_t>(y) * kWidth +
                                            static_cast<std::size_t>(sx)) * 4;
                for (int c = 0; c < 4; ++c) acc[c] += transformed_px[sample + c] * weight;
                wsum += weight;
            }
            for (int c = 0; c < 4; ++c) blurred_px[at(x, y) + c] = acc[c] / wsum;
        }
    }

    // composite.comp (Normal, premultiplied source-over): src + dst * (1 - src.a).
    auto composited_px = background_px;
    for (std::size_t p = 0; p < kPixelCount; ++p) {
        const float src_a = blurred_px[p * 4 + 3];
        for (int c = 0; c < 3; ++c) {
            composited_px[p * 4 + c] =
                blurred_px[p * 4 + c] + background_px[p * 4 + c] * (1.0f - src_a);
        }
        composited_px[p * 4 + 3] = src_a + background_px[p * 4 + 3] * (1.0f - src_a);
    }

    // color_adjust.comp: clamp((rgb + b - 0.5) * contrast + 0.5), then mix tint.
    constexpr float kBrightness = 0.1f;
    constexpr float kContrast = 1.2f;
    constexpr float kTintAmount = 0.25f;
    const float tint[3] = {0.2f, 0.8f, 0.4f};
    auto expected = composited_px;
    for (std::size_t p = 0; p < kPixelCount; ++p) {
        if (composited_px[p * 4 + 3] <= 0.0f) continue;  // transparent passthrough
        for (int c = 0; c < 3; ++c) {
            const float adjusted = std::clamp(
                (composited_px[p * 4 + c] + kBrightness - 0.5f) * kContrast + 0.5f,
                0.0f, 1.0f);
            expected[p * 4 + c] =
                adjusted * (1.0f - kTintAmount) + tint[c] * kTintAmount;
        }
    }

    const auto submissions_before = backend.stats().submissions;
    REQUIRE(execute_command_plan(backend, registry, plan));
    // The 4-pass medium scene coalesces into exactly one queue submission.
    CHECK(backend.stats().submissions == submissions_before + 1);

    std::vector<float> result(kFloatCount, 0.0f);
    REQUIRE(backend.download_surface(output, result).ok());

    // Full-frame parity: the barriers ordered the aliased write-after-read
    // chain so the GPU output matches the CPU oracle pixel-for-pixel.
    const auto report = chronon3d::graph::compare_pixels(expected, result);
    CHECK(report.matched);
    CHECK(report.mismatched_pixels == 0);

    // Semantic spot checks: the red overlay survived transform→blur→composite→
    // grade and outshines the far background, proving the chain actually ran.
    CHECK(result[at(2, 3) + 0] > 0.5f);
    CHECK(result[at(2, 3) + 0] > result[at(0, 0) + 0]);
}

TEST_CASE("physical slots back several logical handles with one VkImage (aliasing, no double free)") {
    using namespace chronon3d::runtime;
    chronon3d::backends::vulkan::VulkanBackend backend;
    RenderSurfaceRegistry registry;
    const SurfaceDesc surface_desc{4, 4, PixelFormat::Rgba32Float,
                                   ResourceUsage::Storage,
                                   LifetimeClass::FrameTransient, 0};
    const auto background = registry.create(surface_desc);
    const auto t1 = registry.create(surface_desc);
    const auto t2 = registry.create(surface_desc);
    const auto output = registry.create(surface_desc);
    REQUIRE(background != kInvalidRenderSurfaceHandle);
    REQUIRE(t1 != kInvalidRenderSurfaceHandle);
    REQUIRE(t2 != kInvalidRenderSurfaceHandle);
    REQUIRE(output != kInvalidRenderSurfaceHandle);
    for (const auto handle : {background, t1, t2, output}) {
        REQUIRE(backend.create_surface(handle, surface_desc).ok());
    }

    std::vector<float> bg(4 * 4 * 4, 0.0f);
    const auto center = (static_cast<std::size_t>(2) * 4 + 2) * 4;
    bg[center + 0] = 0.8f;
    bg[center + 3] = 1.0f;
    REQUIRE(backend.upload_surface(background, surface_desc, bg).ok());

    const ResourceDesc resource_desc{4, 4, PixelFormat::Rgba32Float,
                                     ResourceUsage::Storage,
                                     4 * 4 * 4 * sizeof(float)};
    GpuCommandPlanner planner;
    planner.declare_surface(background, resource_desc);
    planner.declare_surface(t1, resource_desc);
    planner.declare_surface(t2, resource_desc);
    planner.declare_surface(output, resource_desc);
    planner.transform(TransformPass{t1, background, 0, 0, 1.0f});
    planner.blur(BlurPass{t2, t1, 1.5f, 1});
    planner.color_adjust(ColorAdjustPass{output, t2, 0.0f, 1.0f, 0.0f,
                                         {1.0f, 1.0f, 1.0f, 1.0f}});
    const auto plan = planner.build();
    REQUIRE(plan.passes.size() == 3);

    const auto submissions_before = backend.stats().submissions;
    REQUIRE(execute_command_plan(backend, registry, plan));
    CHECK(backend.stats().submissions == submissions_before + 1);

    // Backend-side aliasing proof: lifetime-disjoint transient surfaces are
    // backed by FEWER VkImages than logical handles (t1 and output share one
    // physical slot here), because ownership lives per slot, not per handle.
    const auto physical_after_plan = backend.physical_surface_count();
    CHECK(physical_after_plan > 0);
    CHECK(physical_after_plan < 4);

    std::vector<float> result(4 * 4 * 4, 0.0f);
    REQUIRE(backend.download_surface(output, result).ok());
    // The background center survived transform→blur→color adjust through the
    // shared-slot chain (the per-slot barrier mapper ordered the aliased
    // write-after-read on t1/output's shared image).
    CHECK(result[center + 0] > 0.0f);
    CHECK(result[center + 3] > 0.0f);

    // Releasing ONE handle must never invalidate a sibling aliasing the same
    // slot: the backing image is destroyed only when no handle references it
    // (ownership separated from identity → no double free).
    REQUIRE(backend.release_surface(t1).ok());
    std::vector<float> sibling(4 * 4 * 4, 0.0f);
    REQUIRE(backend.download_surface(output, sibling).ok());
    CHECK(backend.physical_surface_count() <= physical_after_plan);

    // Releasing every remaining handle frees every backing image exactly
    // once; the count reaches zero and downloads of released surfaces fail.
    REQUIRE(backend.release_surface(output).ok());
    REQUIRE(backend.release_surface(t2).ok());
    REQUIRE(backend.release_surface(background).ok());
    CHECK(backend.physical_surface_count() == 0);
    std::vector<float> gone(4 * 4 * 4, 0.0f);
    CHECK_FALSE(backend.download_surface(output, gone).ok());
}

TEST_CASE("N overlays coalesce into one command batch (single vkQueueSubmit)") {
    using namespace chronon3d::runtime;
    chronon3d::backends::vulkan::VulkanBackend backend;
    RenderSurfaceRegistry registry;
    const SurfaceDesc surface_desc{4, 4, PixelFormat::Rgba32Float,
                                   ResourceUsage::Storage,
                                   LifetimeClass::FrameTransient, 0};
    const ResourceDesc resource_desc{4, 4, PixelFormat::Rgba32Float,
                                     ResourceUsage::Storage,
                                     4 * 4 * 4 * sizeof(float)};
    const auto center = (static_cast<std::size_t>(2) * 4 + 2) * 4;

    // Three independent overlays, each: upload a distinct background →
    // identity transform → distinct output.  Distinct values prove the three
    // outputs do not cross-contaminate inside the shared command batch.
    constexpr std::size_t kOverlays = 3;
    std::vector<RenderSurfaceHandle> backgrounds;
    std::vector<RenderSurfaceHandle> outputs;
    std::vector<CommandPlan> plans;
    backgrounds.reserve(kOverlays);
    outputs.reserve(kOverlays);
    plans.reserve(kOverlays);
    for (std::size_t i = 0; i < kOverlays; ++i) {
        const auto background = registry.create(surface_desc);
        const auto output = registry.create(surface_desc);
        REQUIRE(background != kInvalidRenderSurfaceHandle);
        REQUIRE(output != kInvalidRenderSurfaceHandle);
        REQUIRE(backend.create_surface(background, surface_desc).ok());
        REQUIRE(backend.create_surface(output, surface_desc).ok());

        std::vector<float> bg(4 * 4 * 4, 0.0f);
        bg[center + 0] = 0.2f * static_cast<float>(i + 1);
        bg[center + 3] = 1.0f;
        REQUIRE(backend.upload_surface(background, surface_desc, bg).ok());

        GpuCommandPlanner planner;
        planner.declare_surface(background, resource_desc);
        planner.declare_surface(output, resource_desc);
        planner.transform(TransformPass{output, background, 0, 0, 1.0f});
        plans.push_back(planner.build());
        backgrounds.push_back(background);
        outputs.push_back(output);
    }

    const auto submissions_before = backend.stats().submissions;
    backend.begin_command_batch();
    for (const auto& plan : plans) {
        REQUIRE(execute_command_plan(backend, registry, plan));
    }
    backend.end_command_batch();
    // All N overlays were recorded into one command batch: exactly one queue
    // submission for the whole set, not one per overlay.
    CHECK(backend.stats().submissions == submissions_before + 1);

    // Each overlay's output survives independently (no cross-contamination).
    for (std::size_t i = 0; i < kOverlays; ++i) {
        std::vector<float> result(4 * 4 * 4, 0.0f);
        REQUIRE(backend.download_surface(outputs[i], result).ok());
        CHECK(result[center + 0] > 0.0f);
        CHECK(result[center + 3] > 0.0f);
    }
}

TEST_CASE("GPU asset cache reuses uploads and evicts by byte budget") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    chronon3d::runtime::RenderSurfaceRegistry registry;
    chronon3d::runtime::GpuAssetCache cache(64);
    cache.attach(registry, backend);

    const chronon3d::runtime::SurfaceDesc desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, 0};
    const std::vector<float> pixels(16, 1.0f);
    chronon3d::runtime::GpuAssetKey first{};
    first.content_digest.bytes[0] = std::byte{0x01};
    first.format = desc.format;
    first.width = desc.width;
    first.height = desc.height;

    const auto uploaded = cache.acquire(first, desc, pixels);
    REQUIRE(uploaded.ok());
    CHECK_FALSE(uploaded.cache_hit);
    const auto reused = cache.acquire(first, desc, pixels);
    REQUIRE(reused.ok());
    CHECK(reused.cache_hit);
    CHECK(reused.handle == uploaded.handle);
    CHECK(cache.stats().upload_bytes == 64);
    CHECK(cache.stats().hits == 1);

    auto second = first;
    second.content_digest.bytes[0] = std::byte{0x02};
    const auto second_upload = cache.acquire(second, desc, pixels);
    REQUIRE(second_upload.ok());
    CHECK(cache.stats().evictions == 1);
    CHECK(cache.stats().resident_bytes == 64);
    CHECK(registry.lookup(uploaded.handle) == nullptr);
}

TEST_CASE("VRAM glyph atlas keeps glyphs resident and preserves placement metrics") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    chronon3d::runtime::RenderSurfaceRegistry registry;
    chronon3d::runtime::GpuGlyphAtlas atlas;
    atlas.attach(registry, backend);

    const std::vector<float> glyph_coverage(4 * 4, 0.5f);
    const chronon3d::runtime::GpuGlyphKey glyph_a{"font.ttf", 65, 32};
    const chronon3d::runtime::GpuGlyphMetrics metrics_a{-1, -2, 8.0f};

    const auto first = atlas.acquire(glyph_a, 4, 4, glyph_coverage, metrics_a);
    CHECK(first.local_x == -1);
    CHECK(first.local_y == -2);
    CHECK(first.width == 4);
    CHECK(first.height == 4);
    CHECK(first.advance_x == 8.0f);

    // Same glyph again: device-resident hit, same page, same uv_index
    const auto second = atlas.acquire(glyph_a, 4, 4, glyph_coverage, metrics_a);
    CHECK(second.atlas_page == first.atlas_page);
    CHECK(second.uv_index == first.uv_index);
    CHECK(second.advance_x == 8.0f);

    // Metrics are queryable without touching the device.
    const auto stored = atlas.metrics(glyph_a);
    REQUIRE(stored.has_value());
    CHECK(stored->advance_x == 8.0f);

    // A different glyph is a miss with its own uv_index.
    const chronon3d::runtime::GpuGlyphKey glyph_b{"font.ttf", 66, 32};
    const auto third = atlas.acquire(glyph_b, 4, 4, glyph_coverage,
                                     chronon3d::runtime::GpuGlyphMetrics{0, 0, 9.0f});
    CHECK(third.uv_index != first.uv_index);
    CHECK(atlas.stats().hits == 1);
    CHECK(atlas.stats().misses == 2);
    CHECK(atlas.stats().entries == 2);

    atlas.clear();
    CHECK(atlas.stats().entries == 0);
    CHECK_FALSE(atlas.metrics(glyph_a).has_value());
}

TEST_CASE("MTSDF glyph identity ignores runtime font size") {
    using chronon3d::runtime::GlyphRepresentation;
    using chronon3d::runtime::GpuGlyphKey;

    GpuGlyphKey mtsdf_a{"font.ttf", 65, 0x1234, GlyphRepresentation::Mtsdf, 7, 32};
    GpuGlyphKey mtsdf_b = mtsdf_a;
    mtsdf_b.font_size = 96;
    CHECK(mtsdf_a == mtsdf_b);
    CHECK(chronon3d::runtime::GpuGlyphKeyHash{}(mtsdf_a) ==
          chronon3d::runtime::GpuGlyphKeyHash{}(mtsdf_b));

    GpuGlyphKey coverage_b = mtsdf_b;
    coverage_b.representation = GlyphRepresentation::Coverage;
    CHECK_FALSE(mtsdf_a == coverage_b);
    coverage_b.font_size = mtsdf_a.font_size;
    GpuGlyphKey coverage_a = mtsdf_a;
    coverage_a.representation = GlyphRepresentation::Coverage;
    CHECK(coverage_a == coverage_b);
    coverage_b.font_size = 128;
    CHECK_FALSE(coverage_a == coverage_b);
}

TEST_CASE("CPU vs GPU parity harness matches the medium scene with speedup + pixel error") {
    using namespace chronon3d::runtime;
    chronon3d::backends::vulkan::VulkanBackend backend;
    RenderSurfaceRegistry registry;

    constexpr std::uint32_t kWidth = 64;
    constexpr std::uint32_t kHeight = 64;
    constexpr std::size_t kPixels = kWidth * kHeight;
    constexpr std::size_t kFloats = kPixels * 4;
    const SurfaceDesc surface_desc{kWidth, kHeight, PixelFormat::Rgba32Float,
                                   ResourceUsage::Storage,
                                   LifetimeClass::FrameTransient, 0};

    const auto overlay = registry.create(surface_desc);
    const auto transformed = registry.create(surface_desc);
    const auto blurred = registry.create(surface_desc);
    const auto background = registry.create(surface_desc);
    const auto output = registry.create(surface_desc);
    REQUIRE(overlay != kInvalidRenderSurfaceHandle);
    REQUIRE(transformed != kInvalidRenderSurfaceHandle);
    REQUIRE(blurred != kInvalidRenderSurfaceHandle);
    REQUIRE(background != kInvalidRenderSurfaceHandle);
    REQUIRE(output != kInvalidRenderSurfaceHandle);
    for (const auto handle : {overlay, transformed, blurred, background, output}) {
        REQUIRE(backend.create_surface(handle, surface_desc).ok());
    }

    // Inputs: one opaque premultiplied red overlay pixel near the centre over
    // an opaque constant background.
    std::vector<float> overlay_px(kFloats, 0.0f);
    const std::size_t centre =
        (static_cast<std::size_t>(kHeight / 2) * kWidth + kWidth / 2) * 4;
    overlay_px[centre + 0] = 1.0f;
    overlay_px[centre + 3] = 1.0f;
    std::vector<float> background_px(kFloats, 0.0f);
    for (std::size_t p = 0; p < kPixels; ++p) {
        background_px[p * 4 + 0] = 0.2f;
        background_px[p * 4 + 1] = 0.3f;
        background_px[p * 4 + 2] = 0.4f;
        background_px[p * 4 + 3] = 1.0f;
    }
    REQUIRE(backend.upload_surface(overlay, surface_desc, overlay_px).ok());
    REQUIRE(backend.upload_surface(background, surface_desc, background_px).ok());

    // Medium scene: transform → blur → composite → color adjust.
    const ResourceDesc resource_desc{kWidth, kHeight, PixelFormat::Rgba32Float,
                                     ResourceUsage::Storage,
                                     kFloats * sizeof(float)};
    GpuCommandPlanner planner;
    planner.declare_surface(overlay, resource_desc);
    planner.declare_surface(transformed, resource_desc);
    planner.declare_surface(blurred, resource_desc);
    planner.declare_surface(background, resource_desc);
    planner.declare_surface(output, resource_desc);
    planner.transform(TransformPass{transformed, overlay, 1, 0, 1.0f});
    planner.blur(BlurPass{blurred, transformed, 0.5f, 1});
    planner.composite(CompositePass{background, blurred, 0});
    planner.color_adjust(ColorAdjustPass{output, background, 0.1f, 1.2f, 0.25f,
                                         {0.2f, 0.8f, 0.4f, 1.0f}});
    const auto plan = planner.build();
    REQUIRE(plan.passes.size() == 4);

    const std::unordered_map<RenderSurfaceHandle, SurfaceDesc> surfaces{
        {overlay, surface_desc}, {transformed, surface_desc}, {blurred, surface_desc},
        {background, surface_desc}, {output, surface_desc}};
    const std::unordered_map<RenderSurfaceHandle, std::vector<float>> inputs{
        {overlay, overlay_px}, {background, background_px}};

    const auto parity = chronon3d::test::run_cpu_gpu_parity(
        backend, registry, plan, surfaces, inputs, output);

    // Parity is the correctness gate; the timings are informational because a
    // 64x64 scene is dominated by GPU driver overhead (no speedup gate here —
    // the real speedup must be measured on the A4000/WBH at 1920x1080).
    CHECK(parity.matched);
    CHECK(parity.comparison.mismatched_pixels == 0);
    MESSAGE("CPU vs GPU parity: cpu=" << parity.cpu_ms << "ms gpu=" << parity.gpu_ms
            << "ms speedup=" << parity.speedup << "x max_delta="
            << parity.comparison.max_delta << " mean_delta="
            << parity.comparison.mean_delta);
}

TEST_CASE("GPU readiness gate certifies the 7-point checklist") {
    using namespace chronon3d::runtime;
    chronon3d::backends::vulkan::VulkanBackend backend;
    RenderSurfaceRegistry registry;

    // 1920x1080 matches the benchmark target; it is the scale where the GPU
    // actually outruns the scalar CPU reference (unlike the tiny 64x64 probe).
    constexpr std::uint32_t kWidth = 1920;
    constexpr std::uint32_t kHeight = 1080;
    constexpr std::size_t kPixels = kWidth * kHeight;
    constexpr std::size_t kFloats = kPixels * 4;
    const SurfaceDesc surface_desc{kWidth, kHeight, PixelFormat::Rgba32Float,
                                   ResourceUsage::Storage,
                                   LifetimeClass::FrameTransient, 0};

    const auto overlay = registry.create(surface_desc);
    const auto transformed = registry.create(surface_desc);
    const auto blurred = registry.create(surface_desc);
    const auto background = registry.create(surface_desc);
    const auto output = registry.create(surface_desc);
    REQUIRE(overlay != kInvalidRenderSurfaceHandle);
    REQUIRE(transformed != kInvalidRenderSurfaceHandle);
    REQUIRE(blurred != kInvalidRenderSurfaceHandle);
    REQUIRE(background != kInvalidRenderSurfaceHandle);
    REQUIRE(output != kInvalidRenderSurfaceHandle);
    for (const auto handle : {overlay, transformed, blurred, background, output}) {
        REQUIRE(backend.create_surface(handle, surface_desc).ok());
    }

    // Inputs: one opaque premultiplied red overlay pixel at the centre over an
    // opaque constant background.
    std::vector<float> overlay_px(kFloats, 0.0f);
    const std::size_t centre =
        (static_cast<std::size_t>(kHeight / 2) * kWidth + kWidth / 2) * 4;
    overlay_px[centre + 0] = 1.0f;
    overlay_px[centre + 3] = 1.0f;
    std::vector<float> background_px(kFloats, 0.0f);
    for (std::size_t p = 0; p < kPixels; ++p) {
        background_px[p * 4 + 0] = 0.2f;
        background_px[p * 4 + 1] = 0.3f;
        background_px[p * 4 + 2] = 0.4f;
        background_px[p * 4 + 3] = 1.0f;
    }
    REQUIRE(backend.upload_surface(overlay, surface_desc, overlay_px).ok());
    REQUIRE(backend.upload_surface(background, surface_desc, background_px).ok());

    // Medium scene: transform → blur → composite → color adjust.
    const ResourceDesc resource_desc{kWidth, kHeight, PixelFormat::Rgba32Float,
                                     ResourceUsage::Storage,
                                     kFloats * sizeof(float)};
    GpuCommandPlanner planner;
    planner.declare_surface(overlay, resource_desc);
    planner.declare_surface(transformed, resource_desc);
    planner.declare_surface(blurred, resource_desc);
    planner.declare_surface(background, resource_desc);
    planner.declare_surface(output, resource_desc);
    planner.transform(TransformPass{transformed, overlay, 1, 0, 1.0f});
    planner.blur(BlurPass{blurred, transformed, 0.5f, 1});
    planner.composite(CompositePass{background, blurred, 0});
    planner.color_adjust(ColorAdjustPass{output, background, 0.1f, 1.2f, 0.25f,
                                         {0.2f, 0.8f, 0.4f, 1.0f}});
    const auto plan = planner.build();
    REQUIRE(plan.passes.size() == 4);

    const std::unordered_map<RenderSurfaceHandle, SurfaceDesc> surfaces{
        {overlay, surface_desc}, {transformed, surface_desc}, {blurred, surface_desc},
        {background, surface_desc}, {output, surface_desc}};
    const std::unordered_map<RenderSurfaceHandle, std::vector<float>> inputs{
        {overlay, overlay_px}, {background, background_px}};

    const auto gate = chronon3d::test::certify_gpu_readiness(
        backend, registry, plan, surfaces, inputs, output);

    // All seven points must hold: this is the milestone gate that the first
    // real Vulkan render pipeline is operational.
    REQUIRE(gate.output_correct);
    REQUIRE(gate.parity_ok);
    REQUIRE(gate.one_submit_per_frame);
    REQUIRE(gate.no_inter_pass_upload);
    REQUIRE(gate.no_intermediate_readback);
    REQUIRE(gate.physical_lt_logical);
    REQUIRE(gate.gpu_faster);
    CHECK(gate.passed() == chronon3d::test::GpuReadinessGate::kTotalPoints);

    MESSAGE("readiness gate: " << gate.passed() << "/7 speedup=" << gate.speedup
            << "x cpu=" << gate.cpu_ms << "ms gpu=" << gate.gpu_ms
            << "ms plan_physical/logical=" << gate.physical_surfaces << "/"
            << gate.logical_surfaces
            << " backend_physical=" << backend.physical_surface_count()
            << " max_delta=" << gate.pixels.max_delta
            << " mean_delta=" << gate.pixels.mean_delta);
}
#endif

TEST_CASE("overlay template cache compiles a template once per structural descriptor") {
    using namespace chronon3d::runtime;
    OverlayTemplateCache cache(2);  // two-entry capacity → third distinct evicts

    const OverlayTemplateDesc factory{64, 64, 5, 2, 1, true};
    std::size_t build_count = 0;
    const auto builder = [&](const OverlayTemplateDesc& desc) -> CommandPlan {
        ++build_count;
        GpuCommandPlanner planner;
        const ResourceDesc rd{desc.width, desc.height,
                              make_frame_format(PixelFormat::Rgba32Float),
                              ResourceUsage::Storage, LifetimeClass::FrameTransient,
                              static_cast<std::size_t>(desc.width) * desc.height *
                                  4 * sizeof(float)};
        RenderSurfaceHandle slot = 100;
        const std::uint32_t total =
            desc.image_layers + desc.text_layers + desc.logo_layers;
        for (std::uint32_t i = 0; i < total; ++i) {
            const auto src = slot++;
            const auto dst = slot++;
            planner.declare_surface(src, rd);
            planner.declare_surface(dst, rd);
            planner.transform(TransformPass{dst, src, 0, 0, 1.0f});
        }
        return planner.build();
    };

    const auto first = cache.compile(factory, builder);
    CHECK(build_count == 1);
    CHECK(first.plan.pass_count() == 8);

    // Same descriptor → cache hit, the builder is NOT re-invoked.
    const auto second = cache.compile(factory, builder);
    CHECK(build_count == 1);
    CHECK(second.plan.pass_count() == first.plan.pass_count());
    CHECK(cache.stats().hits == 1);
    CHECK(cache.stats().misses == 1);
    CHECK(cache.stats().entries == 1);

    // Different structural descriptor → a separate compile.
    const OverlayTemplateDesc other{64, 64, 1, 1, 0, false};
    (void)cache.compile(other, builder);
    CHECK(build_count == 2);
    CHECK(cache.stats().entries == 2);

    // A third distinct descriptor evicts the LRU tail (capacity 2).
    const OverlayTemplateDesc extra{32, 32, 0, 0, 1, false};
    (void)cache.compile(extra, builder);
    CHECK(build_count == 3);
    CHECK(cache.stats().entries == 2);
    CHECK(cache.stats().evictions >= 1);

    cache.clear();
    CHECK(cache.stats().entries == 0);
}

TEST_CASE("resource planner preserves opaque surface identity across aliasing") {
    using namespace chronon3d::runtime;
    ResourcePlanner planner;
    const RenderSurfaceHandle first{41};
    const RenderSurfaceHandle second{42};
    ResourceDesc desc{64, 64, make_frame_format(PixelFormat::Rgba8Unorm),
                      ResourceUsage::ColorAttachment, LifetimeClass::FrameTransient,
                      64 * 64 * 4};
    planner.add(ResourceRequest{"first", ResourceKind::Color, desc.bytes,
                                LifetimeClass::FrameTransient, 0, 1,
                                alignof(std::max_align_t), desc, first});
    planner.add(ResourceRequest{"second", ResourceKind::Color, desc.bytes,
                                LifetimeClass::FrameTransient, 2, 3,
                                alignof(std::max_align_t), desc, second});

    const auto plan = planner.build();
    REQUIRE(plan.allocations.size() == 2);
    CHECK(plan.allocations[0].surface == first);
    CHECK(plan.allocations[1].surface == second);
    CHECK(plan.allocations[0].physical_slot == plan.allocations[1].physical_slot);
}

TEST_CASE("surface registry owns logical identity independently of physical slots") {
    using namespace chronon3d::runtime;
    RenderSurfaceRegistry registry;
    CHECK(registry.create(SurfaceDesc{}) == kInvalidRenderSurfaceHandle);

    const auto handle = registry.create(SurfaceDesc{
        16, 8, PixelFormat::Rgba8Unorm, ResourceUsage::Storage,
        LifetimeClass::FrameTransient, 0});
    REQUIRE(handle != kInvalidRenderSurfaceHandle);
    REQUIRE(registry.lookup(handle) != nullptr);
    CHECK(registry.lookup(handle)->desc.bytes == 16u * 8u * 4u);
    CHECK(registry.lookup(handle)->physical_slot == std::numeric_limits<std::size_t>::max());
    CHECK(registry.bind_physical_slot(handle, 3));
    CHECK(registry.lookup(handle)->physical_slot == 3);
    CHECK(registry.size() == 1);
    CHECK(registry.release(handle));
    CHECK(registry.lookup(handle) == nullptr);
    CHECK_FALSE(registry.release(handle));
}

TEST_CASE("framebuffer copies preserve opaque surface identity") {
    chronon3d::Framebuffer original(2, 2);
    original.set_surface_handle(77);
    chronon3d::Framebuffer copy(original);
    CHECK(copy.surface_handle() == 77);
    chronon3d::Framebuffer moved(std::move(copy));
    CHECK(moved.surface_handle() == 77);
    moved.clear_surface_handle();
    CHECK(moved.surface_handle() == chronon3d::runtime::kInvalidRenderSurfaceHandle);
}

TEST_CASE("command planner preserves pass order and kinds") {
    using namespace chronon3d::runtime;
    GpuCommandPlanner planner;
    planner.composite(CompositePass{.destination = 1, .source = 2, .blend_mode = 0});
    planner.transform(TransformPass{.destination = 3, .source = 1,
                                    .offset_x = 1, .offset_y = 1, .opacity = 0.5f});
    planner.matte(MattePass{.destination = 4, .target = 3, .matte = 2,
                            .luma = 0, .inverted = 0});

    const auto plan = planner.build();
    REQUIRE(plan.pass_count() == 3);
    CHECK(plan.passes.passes[0].kind == GpuPassKind::Composite);
    CHECK(plan.passes.passes[1].kind == GpuPassKind::Transform);
    CHECK(plan.passes.passes[2].kind == GpuPassKind::Matte);
    CHECK(std::get<TransformPass>(plan.passes.passes[1].params).destination == 3);
    CHECK(std::get<MattePass>(plan.passes.passes[2].params).target == 3);
}

TEST_CASE("command planner aliases non-overlapping transient surfaces") {
    using namespace chronon3d::runtime;
    GpuCommandPlanner planner;
    const RenderSurfaceHandle input{1};
    const RenderSurfaceHandle scratch{2};
    const RenderSurfaceHandle output{3};
    const ResourceDesc desc{16, 8, make_frame_format(PixelFormat::Rgba8Unorm),
                            ResourceUsage::Storage, LifetimeClass::FrameTransient,
                            16 * 8 * 4};
    planner.declare_surface(input, desc);
    planner.declare_surface(scratch, desc);
    planner.declare_surface(output, desc);

    // scratch (0..1) bridges the two passes; input (0..0) and output (1..1)
    // never overlap so the planner must alias them onto one physical slot.
    planner.blur(BlurPass{.destination = scratch, .source = input,
                          .radius = 2.0f, .horizontal = 1});
    planner.blur(BlurPass{.destination = output, .source = scratch,
                          .radius = 2.0f, .horizontal = 0});

    const auto plan = planner.build();
    REQUIRE(plan.pass_count() == 2);

    const auto slot_for = [&](RenderSurfaceHandle handle) {
        for (const auto& allocation : plan.resources.allocations) {
            if (allocation.surface == handle) return allocation.physical_slot;
        }
        return std::numeric_limits<std::size_t>::max();
    };

    const auto input_slot = slot_for(input);
    const auto scratch_slot = slot_for(scratch);
    const auto output_slot = slot_for(output);
    CHECK(input_slot != std::numeric_limits<std::size_t>::max());
    CHECK(scratch_slot != std::numeric_limits<std::size_t>::max());
    CHECK(output_slot != std::numeric_limits<std::size_t>::max());
    CHECK(input_slot == output_slot);
    CHECK(input_slot != scratch_slot);
}

TEST_CASE("command planner emits read and write barriers per pass") {
    using namespace chronon3d::runtime;
    GpuCommandPlanner planner;
    const RenderSurfaceHandle input{1};
    const RenderSurfaceHandle scratch{2};
    const RenderSurfaceHandle output{3};
    const ResourceDesc rd{16, 8, make_frame_format(PixelFormat::Rgba8Unorm),
                          ResourceUsage::Storage, LifetimeClass::FrameTransient,
                          16 * 8 * 4};
    planner.declare_surface(input, rd);
    planner.declare_surface(scratch, rd);
    planner.declare_surface(output, rd);
    planner.blur(BlurPass{.destination = scratch, .source = input,
                          .radius = 1.0f, .horizontal = 1});
    planner.blur(BlurPass{.destination = output, .source = scratch,
                          .radius = 1.0f, .horizontal = 0});

    const auto plan = planner.build();
    // Canonical transition stream: first-write + RAW per bridging surface.
    // Pass 0: input read→scratch written; pass 1: scratch read (write→read
    // transition) and output written.
    auto transition_for = [&plan](std::size_t pass_index,
                                  RenderSurfaceHandle surface)
        -> const ResourceTransition* {
        for (const auto& t : plan.transitions) {
            if (t.consumer_pass != pass_index) continue;
            if (t.resource < plan.resources.requests.size() &&
                plan.resources.requests[t.resource].surface == surface) {
                return &t;
            }
        }
        return nullptr;
    };

    const auto* input_read = transition_for(0, input);
    REQUIRE(input_read != nullptr);
    const auto input_read_ok =
        input_read->before.reads() || input_read->after.reads();
    CHECK(input_read_ok);
    const auto* scratch_write = transition_for(0, scratch);
    REQUIRE(scratch_write != nullptr);
    CHECK(scratch_write->after.writes());
    // Pass 1: scratch sampled (Read) — the write→read transition — and
    // output written (Write).
    const auto* scratch_read = transition_for(1, scratch);
    REQUIRE(scratch_read != nullptr);
    CHECK(scratch_read->before.writes());
    CHECK(scratch_read->after.reads());
    const auto* output_write = transition_for(1, output);
    REQUIRE(output_write != nullptr);
    CHECK(output_write->after.writes());
}

TEST_CASE("plan slot binding propagates aliasing to the surface registry") {
    using namespace chronon3d::runtime;
    RenderSurfaceRegistry registry;
    const auto input = registry.create(SurfaceDesc{16, 8, PixelFormat::Rgba8Unorm,
        ResourceUsage::Storage, LifetimeClass::FrameTransient, 0});
    const auto scratch = registry.create(SurfaceDesc{16, 8, PixelFormat::Rgba8Unorm,
        ResourceUsage::Storage, LifetimeClass::FrameTransient, 0});
    const auto output = registry.create(SurfaceDesc{16, 8, PixelFormat::Rgba8Unorm,
        ResourceUsage::Storage, LifetimeClass::FrameTransient, 0});
    REQUIRE(input != kInvalidRenderSurfaceHandle);
    REQUIRE(scratch != kInvalidRenderSurfaceHandle);
    REQUIRE(output != kInvalidRenderSurfaceHandle);

    const ResourceDesc desc{16, 8, make_frame_format(PixelFormat::Rgba8Unorm),
                            ResourceUsage::Storage, LifetimeClass::FrameTransient,
                            16 * 8 * 4};
    GpuCommandPlanner planner;
    planner.declare_surface(input, desc);
    planner.declare_surface(scratch, desc);
    planner.declare_surface(output, desc);
    planner.blur(BlurPass{.destination = scratch, .source = input,
                          .radius = 2.0f, .horizontal = 1});
    planner.blur(BlurPass{.destination = output, .source = scratch,
                          .radius = 2.0f, .horizontal = 0});

    const auto plan = planner.build();
    bind_plan_slots(plan.resources, registry);

    const auto* input_record = registry.lookup(input);
    const auto* scratch_record = registry.lookup(scratch);
    const auto* output_record = registry.lookup(output);
    REQUIRE(input_record != nullptr);
    REQUIRE(scratch_record != nullptr);
    REQUIRE(output_record != nullptr);
    CHECK(input_record->physical_slot != std::numeric_limits<std::size_t>::max());
    CHECK(scratch_record->physical_slot != std::numeric_limits<std::size_t>::max());
    CHECK(output_record->physical_slot != std::numeric_limits<std::size_t>::max());
    CHECK(input_record->physical_slot == output_record->physical_slot);
    CHECK(input_record->physical_slot != scratch_record->physical_slot);
}

TEST_CASE("checkbackend pixel comparison matches identical buffers") {
    using namespace chronon3d::graph;
    const std::vector<float> buffer{0.1f, 0.2f, 0.3f, 0.4f,
                                    0.5f, 0.6f, 0.7f, 0.8f};
    const auto result = compare_pixels(buffer, buffer);
    CHECK(result.matched);
    CHECK(result.mismatched_pixels == 0);
    CHECK(result.max_delta == doctest::Approx(0.0f));
}

TEST_CASE("checkbackend pixel comparison rejects out-of-tolerance channels") {
    using namespace chronon3d::graph;
    const std::vector<float> reference{0.5f, 0.5f, 0.5f, 0.5f};
    std::vector<float> result = reference;
    result[0] = 0.6f;  // delta 0.1 exceeds the relative gate
    const PixelTolerance tolerance{.epsilon = 1e-4f, .absolute = 1e-4f};
    const auto report = compare_pixels(reference, result, tolerance);
    CHECK_FALSE(report.matched);
    CHECK(report.mismatched_pixels == 1);
    CHECK(report.max_delta == doctest::Approx(0.1f).epsilon(1e-4));
}

TEST_CASE("checkbackend pixel comparison respects the absolute tolerance floor") {
    using namespace chronon3d::graph;
    // A near-zero reference makes the relative gate tiny; the absolute floor
    // must still admit a small delta.
    const std::vector<float> reference{0.0f, 0.0f, 0.0f, 0.0f};
    const std::vector<float> result{1e-5f, 0.0f, 0.0f, 0.0f};
    const PixelTolerance tolerance{.epsilon = 1e-4f, .absolute = 1e-4f};
    CHECK(compare_pixels(reference, result, tolerance).matched);
}

TEST_CASE("checkbackend size mismatch is reported as a sentinel failure") {
    using namespace chronon3d::graph;
    const std::vector<float> reference(8, 0.0f);
    const std::vector<float> result(4, 0.0f);
    const auto report = compare_pixels(reference, result);
    CHECK_FALSE(report.matched);
    CHECK(report.mismatched_pixels == std::numeric_limits<std::size_t>::max());
}

TEST_CASE("tight_surface_bytes calculates correct sizes for all formats") {
    using namespace chronon3d::runtime;
    constexpr std::uint32_t w = 1920;
    constexpr std::uint32_t h = 1080;

    CHECK(tight_surface_bytes(PixelFormat::Rgba32Float, w, h) == 1920 * 1080 * 16);
    CHECK(tight_surface_bytes(PixelFormat::Rgba8Unorm, w, h) == 1920 * 1080 * 4);
    CHECK(tight_surface_bytes(PixelFormat::R8Unorm, w, h) == 1920 * 1080 * 1);
    CHECK(tight_surface_bytes(PixelFormat::Nv12, w, h) == 1920 * 1080 * 3 / 2);
    CHECK(tight_surface_bytes(PixelFormat::P010, w, h) == 1920 * 1080 * 3);
    CHECK(tight_surface_bytes(PixelFormat::Depth32Float, w, h) == 1920 * 1080 * 4);

    // Odd dimensions chroma alignment
    constexpr std::uint32_t odd_w = 1919;
    constexpr std::uint32_t odd_h = 1079;
    const std::size_t expected_nv12_odd = 1919 * 1079 + 1920 * 540;
    CHECK(tight_surface_bytes(PixelFormat::Nv12, odd_w, odd_h) == expected_nv12_odd);
}

TEST_CASE("SurfaceDesc::make centralizes format size and slot ownership") {
    using namespace chronon3d::runtime;
    const auto desc = SurfaceDesc::make(
        1920, 1080, PixelFormat::Rgba32Float, ResourceUsage::Storage,
        LifetimeClass::PipelineSlot);
    CHECK(desc.bytes == tight_surface_bytes(desc.format, desc.width, desc.height));
    CHECK(desc.bytes == 1920 * 1080 * 16);
    CHECK(desc.lifetime == LifetimeClass::PipelineSlot);

    const auto resource = ResourceDesc::make(
        1920, 1080, PixelFormat::Rgba32Float, ResourceUsage::Storage,
        LifetimeClass::PipelineSlot);
    CHECK(resource.bytes == desc.bytes);
    CHECK(resource.lifetime == desc.lifetime);
}

TEST_CASE("RenderSurfaceRegistry supports NV12 and P010 multi-format surfaces with ColorMetadata") {
    using namespace chronon3d::runtime;
    RenderSurfaceRegistry registry;

    SurfaceDesc nv12_desc(
        1920,
        1080,
        FrameFormat(
            PixelFormat::Nv12,
            ColorPrimaries::Bt709,
            TransferFunction::Bt1886,
            ColorMatrix::Bt709,
            ColorRange::Limited,
            ChromaLocation::Left,
            AlphaMode::Opaque),
        ResourceUsage::Storage,
        LifetimeClass::JobPersistent,
        0);

    const auto handle = registry.create(nv12_desc);
    REQUIRE(handle != kInvalidRenderSurfaceHandle);

    const auto* record = registry.lookup(handle);
    REQUIRE(record != nullptr);
    CHECK(record->desc.format.pixel == PixelFormat::Nv12);
    CHECK(record->desc.bytes == 1920 * 1080 * 3 / 2);
    CHECK(record->desc.format.matrix == ColorMatrix::Bt709);
    CHECK(record->desc.format.range == ColorRange::Limited);
}
