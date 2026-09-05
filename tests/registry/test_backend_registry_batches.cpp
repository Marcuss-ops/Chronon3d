// test_backend_registry_batches.cpp — Vulkan submission telemetry and
// end-to-end CommandPlan render batches (split from
// test_backend_registry.cpp).
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

#endif
