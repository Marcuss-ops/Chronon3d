// test_backend_registry_surfaces.cpp — surface aliasing, registry identity,
// checkbackend and surface descriptor tests (split from
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
