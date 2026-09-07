// test_backend_registry_vulkan_dispatch.cpp — Split from
// test_backend_registry_vulkan.cpp: TrackMatteNode / SourceNode dispatch
// through Vulkan surfaces and the fill_rect_surface CPU-oracle parity lock
// (Vulkan-gated as in the origin).
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

#ifdef CHRONON3D_ENABLE_VULKAN
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
#endif
