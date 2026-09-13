// test_backend_registry_vulkan_text_run.cpp — Split from
// test_backend_registry_vulkan.cpp: packed text-run glyph packing and
// cached text-run styled-cache coverage (Vulkan-gated as in the origin).
#include <doctest/doctest.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
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
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <blend2d.h>
#endif

#include "../../src/render_graph/nodes/text_run/gpu_text_run.hpp"

#ifdef CHRONON3D_ENABLE_VULKAN
TEST_CASE("packed text-run builder packs glyphs and composites via Vulkan") {
    using namespace chronon3d;
    using namespace chronon3d::graph;
    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;

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
    RenderGraphContext ctx;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;
    ctx.text_render_resources = &text_resources;
    auto runtime_result = runtime::RuntimeContext::create(runtime::RuntimeConfig{});
    REQUIRE(runtime_result.has_value());
    auto runtime = std::move(runtime_result.value());
    ctx.runtime = runtime.get();

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
