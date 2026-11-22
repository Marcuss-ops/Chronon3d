// LUT auxiliary-surface cache regression test.
//
// C3D-AUDIT-02 moved the Curves/Lut3D auxiliary table from a per-frame
// create+upload to GpuAssetCache.  This test measures that claim at the bulk
// dispatch boundary: the first frame populates the cache, the second frame
// must reuse it without any create/upload, and a table change must miss while
// restoring the original table hits again.

#include <doctest/doctest.h>

#include <chronon3d/effects/effect_instance.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#include "src/render_graph/nodes/effect_stack_node_native_api.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace {

using chronon3d::EffectStack;
using chronon3d::Framebuffer;
using chronon3d::graph::RenderBackend;
using chronon3d::graph::RenderGraphContext;
using chronon3d::graph::RenderOpOutcome;
using chronon3d::graph::RenderOpResult;
using chronon3d::runtime::GpuAssetCache;
using chronon3d::runtime::LifetimeClass;
using chronon3d::runtime::PixelFormat;
using chronon3d::runtime::RenderSurfaceHandle;
using chronon3d::runtime::RenderSurfaceRegistry;
using chronon3d::runtime::ResourceUsage;
using chronon3d::runtime::SurfaceDesc;

class LutCacheBackend final : public RenderBackend {
public:
    RenderOpResult create_surface(RenderSurfaceHandle handle,
                                  const SurfaceDesc&) override {
        std::lock_guard lock(mutex_);
        created_.push_back(handle);
        creates.fetch_add(1, std::memory_order_relaxed);
        return RenderOpResult(RenderOpOutcome{});
    }

    RenderOpResult release_surface(RenderSurfaceHandle handle) override {
        std::lock_guard lock(mutex_);
        released_.push_back(handle);
        releases.fetch_add(1, std::memory_order_relaxed);
        return RenderOpResult(RenderOpOutcome{});
    }

    [[nodiscard]] bool is_native_surface_valid(
        RenderSurfaceHandle handle) const noexcept override {
        std::lock_guard lock(mutex_);
        return handle != chronon3d::runtime::kInvalidRenderSurfaceHandle &&
               std::find(created_.begin(), created_.end(), handle) != created_.end() &&
               std::find(released_.begin(), released_.end(), handle) == released_.end();
    }

    RenderOpResult upload_surface(RenderSurfaceHandle,
                                  const SurfaceDesc&,
                                  std::span<const float>) override {
        uploads.fetch_add(1, std::memory_order_relaxed);
        return RenderOpResult(RenderOpOutcome{});
    }

    RenderOpResult lut_bulk_surface(RenderSurfaceHandle destination,
                                    RenderSurfaceHandle source,
                                    RenderSurfaceHandle aux,
                                    const chronon3d::graph::LutBulkParams&) override {
        last_destination.store(destination, std::memory_order_relaxed);
        last_source.store(source, std::memory_order_relaxed);
        last_aux.store(aux, std::memory_order_relaxed);
        lut_dispatches.fetch_add(1, std::memory_order_relaxed);
        return RenderOpResult(RenderOpOutcome{});
    }

    [[nodiscard]] bool was_released(RenderSurfaceHandle handle) const {
        std::lock_guard lock(mutex_);
        return std::find(released_.begin(), released_.end(), handle) != released_.end();
    }

    void apply_effect_stack(
        Framebuffer&, const chronon3d::EffectStack&,
        const chronon3d::effects::EffectExecutionContext&) override {}

    void composite_layer(
        Framebuffer&, const Framebuffer&, chronon3d::BlendMode,
        const std::optional<chronon3d::raster::BBox>& = std::nullopt,
        chronon3d::CompositeOperator = chronon3d::CompositeOperator::SourceOver) override {}

    void apply_blur(
        Framebuffer&, float,
        const std::optional<chronon3d::raster::BBox>& = std::nullopt) override {}

    void apply_per_pixel_dof(
        Framebuffer&, std::span<const float>,
        const chronon3d::DepthOfFieldSettings&, const chronon3d::LensModel&,
        const std::optional<chronon3d::raster::BBox>&) override {}

    mutable std::mutex mutex_;
    std::vector<RenderSurfaceHandle> created_;
    std::vector<RenderSurfaceHandle> released_;
    std::atomic<std::size_t> creates{0};
    std::atomic<std::size_t> releases{0};
    std::atomic<std::size_t> uploads{0};
    std::atomic<std::size_t> lut_dispatches{0};
    std::atomic<RenderSurfaceHandle> last_destination{
        chronon3d::runtime::kInvalidRenderSurfaceHandle};
    std::atomic<RenderSurfaceHandle> last_source{
        chronon3d::runtime::kInvalidRenderSurfaceHandle};
    std::atomic<RenderSurfaceHandle> last_aux{
        chronon3d::runtime::kInvalidRenderSurfaceHandle};
};

std::shared_ptr<const std::vector<std::array<float, 3>>> make_lut_table(
    bool alternate = false) {
    auto values = std::make_shared<std::vector<std::array<float, 3>>>();
    values->assign({
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
    });
    if (alternate) {
        (*values)[0] = {0.2f, 0.2f, 0.2f};
        (*values)[7] = {0.9f, 0.8f, 0.7f};
    }
    return values;
}

void check_lut_cache_steady_state(EffectStack first, EffectStack changed) {
    constexpr int kWidth = 8;
    constexpr int kHeight = 8;

    RenderSurfaceRegistry registry;
    LutCacheBackend backend;
    GpuAssetCache cache;
    cache.attach(registry, backend);

    RenderGraphContext ctx;
    ctx.backend = &backend;
    ctx.surface_registry = &registry;
    ctx.gpu_asset_cache = &cache;

    const auto desc = SurfaceDesc::make(
        kWidth, kHeight, PixelFormat::Rgba32Float,
        ResourceUsage::Storage, LifetimeClass::FrameTransient);
    const auto source = registry.create(desc);
    REQUIRE(source != chronon3d::runtime::kInvalidRenderSurfaceHandle);
    REQUIRE(backend.create_surface(source, desc).ok());

    Framebuffer first_result(kWidth, kHeight);
    REQUIRE(chronon3d::graph::native_effects::try_native_full_frame_lut_bulk(
        ctx, first, first_result, std::nullopt, source));

    const auto first_stats = cache.stats();
    const auto first_creates = backend.creates.load();
    const auto first_uploads = backend.uploads.load();
    const auto cached_aux = backend.last_aux.load();

    CHECK(first_stats.hits == 0);
    CHECK(first_stats.initial_uploads == 1);
    CHECK(first_uploads == 1);
    REQUIRE(cached_aux != chronon3d::runtime::kInvalidRenderSurfaceHandle);

    Framebuffer second_result(kWidth, kHeight);
    REQUIRE(chronon3d::graph::native_effects::try_native_full_frame_lut_bulk(
        ctx, first, second_result, std::nullopt, source));

    const auto second_stats = cache.stats();
    CHECK(second_stats.hits == first_stats.hits + 1);
    CHECK(second_stats.initial_uploads == first_stats.initial_uploads);
    CHECK(backend.uploads.load() == first_uploads);
    CHECK(backend.creates.load() == first_creates + 1);  // destination only
    CHECK_FALSE(backend.was_released(cached_aux));

    Framebuffer changed_result(kWidth, kHeight);
    REQUIRE(chronon3d::graph::native_effects::try_native_full_frame_lut_bulk(
        ctx, changed, changed_result, std::nullopt, source));

    const auto changed_stats = cache.stats();
    CHECK(changed_stats.hits == second_stats.hits);
    CHECK(changed_stats.initial_uploads == first_stats.initial_uploads + 1);
    CHECK(backend.uploads.load() == first_uploads + 1);
    CHECK_FALSE(backend.was_released(cached_aux));

    Framebuffer restored_result(kWidth, kHeight);
    REQUIRE(chronon3d::graph::native_effects::try_native_full_frame_lut_bulk(
        ctx, first, restored_result, std::nullopt, source));

    const auto restored_stats = cache.stats();
    CHECK(restored_stats.hits == changed_stats.hits + 1);
    CHECK(restored_stats.initial_uploads == changed_stats.initial_uploads);
    CHECK(backend.uploads.load() == first_uploads + 1);
    CHECK(backend.lut_dispatches.load() == 4);
    CHECK_FALSE(backend.was_released(cached_aux));

    CHECK(first_result.surface_handle() !=
          chronon3d::runtime::kInvalidRenderSurfaceHandle);
    CHECK(second_result.surface_handle() !=
          chronon3d::runtime::kInvalidRenderSurfaceHandle);
    CHECK(changed_result.surface_handle() !=
          chronon3d::runtime::kInvalidRenderSurfaceHandle);
    CHECK(restored_result.surface_handle() !=
          chronon3d::runtime::kInvalidRenderSurfaceHandle);
}

} // namespace

TEST_CASE("LUT cache: Curves auxiliary surface is uploaded once across frames") {
    EffectStack first;
    first.push_back(chronon3d::effects::EffectInstance{
        chronon3d::CurvesParams{
            .master = {{0.0f, 0.0f}, {0.5f, 0.8f}, {1.0f, 1.0f}}}});

    EffectStack changed;
    changed.push_back(chronon3d::effects::EffectInstance{
        chronon3d::CurvesParams{
            .master = {{0.0f, 0.0f}, {0.5f, 0.4f}, {1.0f, 1.0f}}}});

    check_lut_cache_steady_state(std::move(first), std::move(changed));
}

TEST_CASE("LUT cache: Lut3D auxiliary surface is uploaded once across frames") {
    const auto table = make_lut_table();
    const auto other_table = make_lut_table(true);

    EffectStack first;
    first.push_back(chronon3d::effects::EffectInstance{
        chronon3d::Lut3DParams{
            .dimension = 2, .values = table, .amount = 1.0f}});

    EffectStack changed;
    changed.push_back(chronon3d::effects::EffectInstance{
        chronon3d::Lut3DParams{
            .dimension = 2, .values = other_table, .amount = 0.5f}});

    check_lut_cache_steady_state(std::move(first), std::move(changed));
}
