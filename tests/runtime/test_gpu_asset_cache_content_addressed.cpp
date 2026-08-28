#include <doctest/doctest.h>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

#include <array>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace {

class CacheBackend final : public chronon3d::graph::RenderBackend {
public:
    chronon3d::graph::RenderOpResult create_surface(
        chronon3d::runtime::RenderSurfaceHandle handle,
        const chronon3d::runtime::SurfaceDesc&) override {
        created.push_back(handle);
        return chronon3d::graph::RenderOpResult(chronon3d::graph::RenderOpOutcome{});
    }

    chronon3d::graph::RenderOpResult release_surface(
        chronon3d::runtime::RenderSurfaceHandle handle) override {
        released.push_back(handle);
        return chronon3d::graph::RenderOpResult(chronon3d::graph::RenderOpOutcome{});
    }

    bool is_native_surface_valid(
        chronon3d::runtime::RenderSurfaceHandle handle) const noexcept override {
        return handle != chronon3d::runtime::kInvalidRenderSurfaceHandle &&
               std::find(created.begin(), created.end(), handle) != created.end() &&
               std::find(released.begin(), released.end(), handle) == released.end();
    }

    chronon3d::graph::RenderOpResult upload_surface(
        chronon3d::runtime::RenderSurfaceHandle,
        const chronon3d::runtime::SurfaceDesc&,
        std::span<const float>) override {
        ++uploads;
        return chronon3d::graph::RenderOpResult(chronon3d::graph::RenderOpOutcome{});
    }

    // No-op effect/composite/blur/DOF hooks — this stub exercises only the
    // surface create/upload/release contract used by GpuAssetCache.
    void apply_effect_stack(
        chronon3d::Framebuffer&,
        const chronon3d::EffectStack&,
        const chronon3d::effects::EffectExecutionContext&) override {}

    void composite_layer(
        chronon3d::Framebuffer&,
        const chronon3d::Framebuffer&,
        chronon3d::BlendMode,
        const std::optional<chronon3d::raster::BBox>& = std::nullopt,
        chronon3d::CompositeOperator = chronon3d::CompositeOperator::SourceOver) override {}

    void apply_blur(
        chronon3d::Framebuffer&,
        float,
        const std::optional<chronon3d::raster::BBox>& = std::nullopt) override {}

    void apply_per_pixel_dof(
        chronon3d::Framebuffer&,
        std::span<const float>,
        const chronon3d::DepthOfFieldSettings&,
        const chronon3d::LensModel&,
        const std::optional<chronon3d::raster::BBox>&) override {}

    std::vector<chronon3d::runtime::RenderSurfaceHandle> created;
    std::vector<chronon3d::runtime::RenderSurfaceHandle> released;
    std::size_t uploads{0};
};

chronon3d::runtime::GpuAssetKey key_for(std::string_view content) {
    return {
        chronon3d::assets::sha256_string(content),
        chronon3d::runtime::PixelFormat::Rgba32Float,
        1,
        1,
    };
}

} // namespace

TEST_CASE("GpuAssetCache uses content digests for static logo/background reuse") {
    chronon3d::runtime::RenderSurfaceRegistry registry;
    CacheBackend backend;
    chronon3d::runtime::GpuAssetCache cache;
    cache.attach(registry, backend);

    const std::array<float, 4> logo_pixels{1.0f, 0.0f, 0.0f, 1.0f};
    const auto logo_key = key_for("logo-content-v1");
    const chronon3d::runtime::SurfaceDesc desc{
        1, 1, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent,
        sizeof(logo_pixels)};

    const auto first = cache.acquire(logo_key, desc, logo_pixels);
    REQUIRE(first.ok());
    CHECK_FALSE(first.cache_hit);
    CHECK(backend.uploads == 1);

    const auto alias = cache.acquire(logo_key, desc, logo_pixels);
    REQUIRE(alias.ok());
    CHECK(alias.cache_hit);
    CHECK(alias.handle == first.handle);
    CHECK(backend.uploads == 1);

    const auto changed_background_key = key_for("background-content-v2");
    const auto background = cache.acquire(changed_background_key, desc, logo_pixels);
    REQUIRE(background.ok());
    CHECK_FALSE(background.cache_hit);
    CHECK(background.handle != first.handle);
    CHECK(backend.uploads == 2);

    const auto stats = cache.stats();
    CHECK(stats.initial_uploads == 2);
    CHECK(stats.initial_upload_bytes == 2 * sizeof(logo_pixels));
    CHECK(stats.hits == 1);
    CHECK(stats.misses == 2);
}

TEST_CASE("GpuAssetCache evicts content-addressed static assets by budget") {
    chronon3d::runtime::RenderSurfaceRegistry registry;
    CacheBackend backend;
    chronon3d::runtime::GpuAssetCache cache(sizeof(float) * 4);
    cache.attach(registry, backend);

    const std::array<float, 4> pixels{1.0f, 1.0f, 1.0f, 1.0f};
    const chronon3d::runtime::SurfaceDesc desc{
        1, 1, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent,
        sizeof(pixels)};

    REQUIRE(cache.acquire(key_for("first"), desc, pixels).ok());
    REQUIRE(cache.acquire(key_for("second"), desc, pixels).ok());

    const auto stats = cache.stats();
    CHECK(stats.evictions == 1);
    CHECK(stats.evicted_bytes == sizeof(pixels));
    CHECK(stats.resident_bytes == sizeof(pixels));
}
