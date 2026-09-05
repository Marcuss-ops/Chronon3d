// test_backend_registry_parity.cpp — CPU vs GPU parity, readiness gate and
// template cache tests (split from test_backend_registry.cpp).
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
