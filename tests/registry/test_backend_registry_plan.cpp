// test_backend_registry_plan.cpp — Vulkan frame batch, plan-driven sync and
// command plan executor tests (split from test_backend_registry.cpp).
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

#endif

#ifdef CHRONON3D_ENABLE_VULKAN
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
#endif
