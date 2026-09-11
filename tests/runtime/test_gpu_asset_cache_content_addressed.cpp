#include <doctest/doctest.h>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace {

class CacheBackend final : public chronon3d::graph::RenderBackend {
public:
    chronon3d::graph::RenderOpResult create_surface(
        chronon3d::runtime::RenderSurfaceHandle handle,
        const chronon3d::runtime::SurfaceDesc&) override {
        std::lock_guard lock(mutex_);
        created.push_back(handle);
        return chronon3d::graph::RenderOpResult(chronon3d::graph::RenderOpOutcome{});
    }

    chronon3d::graph::RenderOpResult release_surface(
        chronon3d::runtime::RenderSurfaceHandle handle) override {
        {
            std::lock_guard lock(mutex_);
            released.push_back(handle);
            if (fail_releases) {
                return chronon3d::graph::RenderOpResult(chronon3d::graph::make_render_backend_error(
                    chronon3d::graph::RenderBackendErrorCode::ExecutionFailure,
                    "release_surface: simulated device failure"));
            }
        }
        return chronon3d::graph::RenderOpResult(chronon3d::graph::RenderOpOutcome{});
    }

    bool is_native_surface_valid(
        chronon3d::runtime::RenderSurfaceHandle handle) const noexcept override {
        std::lock_guard lock(mutex_);
        return handle != chronon3d::runtime::kInvalidRenderSurfaceHandle &&
               std::find(created.begin(), created.end(), handle) != created.end() &&
               std::find(released.begin(), released.end(), handle) == released.end() &&
               std::find(invalidated.begin(), invalidated.end(), handle) == invalidated.end();
    }

    chronon3d::graph::RenderOpResult upload_surface(
        chronon3d::runtime::RenderSurfaceHandle,
        const chronon3d::runtime::SurfaceDesc&,
        std::span<const float>) override {
        uploads.fetch_add(1, std::memory_order_relaxed);
        if (upload_delay_us > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(upload_delay_us));
        }
        return chronon3d::graph::RenderOpResult(chronon3d::graph::RenderOpOutcome{});
    }

    /// Emulate a transient backend cleanup that invalidates the physical
    /// surface while the RenderSurfaceRegistry keeps the logical handle.
    void invalidate_physical(chronon3d::runtime::RenderSurfaceHandle handle) {
        std::lock_guard lock(mutex_);
        invalidated.push_back(handle);
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

    mutable std::mutex mutex_;
    std::vector<chronon3d::runtime::RenderSurfaceHandle> created;
    std::vector<chronon3d::runtime::RenderSurfaceHandle> released;
    std::vector<chronon3d::runtime::RenderSurfaceHandle> invalidated;
    std::atomic<std::size_t> uploads{0};
    std::atomic<bool> fail_releases{false};
    std::uint32_t upload_delay_us{0};
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
    CHECK(stats.release_failures == 0);
}

TEST_CASE("GpuAssetCache drops orphaned registry handle when the cached surface goes stale") {
    chronon3d::runtime::RenderSurfaceRegistry registry;
    CacheBackend backend;
    chronon3d::runtime::GpuAssetCache cache;
    cache.attach(registry, backend);

    const std::array<float, 4> pixels{0.5f, 0.5f, 0.5f, 1.0f};
    const chronon3d::runtime::SurfaceDesc desc{
        1, 1, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent,
        sizeof(pixels)};
    const auto key = key_for("stale-asset");

    const auto first = cache.acquire(key, desc, pixels);
    REQUIRE(first.ok());
    REQUIRE(registry.size() == 1);

    // Transient backend cleanup kills the physical surface while the logical
    // registry handle survives — the exact stale-entry pattern that used to
    // leak the registry entry.
    backend.invalidate_physical(first.handle);

    const auto rebuilt = cache.acquire(key, desc, pixels);
    REQUIRE(rebuilt.ok());
    CHECK_FALSE(rebuilt.cache_hit);
    CHECK(rebuilt.handle != first.handle);
    // Only the rebuilt handle may remain in the registry — the stale one must
    // have been released, otherwise the registry grows on every invalidation.
    CHECK(registry.size() == 1);
    CHECK(backend.uploads == 2);

    const auto stats = cache.stats();
    CHECK(stats.evictions == 1);
    CHECK(stats.evicted_bytes == sizeof(pixels));
    CHECK(stats.resident_bytes == sizeof(pixels));
    CHECK(stats.release_failures == 0);
}

TEST_CASE("GpuAssetCache keeps residency charged when the backend release fails") {
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

    backend.fail_releases = true;
    REQUIRE(cache.acquire(key_for("first"), desc, pixels).ok());
    REQUIRE(cache.acquire(key_for("second"), desc, pixels).ok());

    // The device refused to free the evicted surface: the cache must not
    // claim it reclaimed the memory or count the eviction as successful.
    auto stats = cache.stats();
    CHECK(stats.release_failures == 2);
    CHECK(stats.evictions == 0);
    CHECK(stats.evicted_bytes == 0);
    CHECK(stats.resident_bytes == 2 * sizeof(pixels));

    // Both entries are gone from the cache bookkeeping (a subsequent acquire
    // re-uploads instead of handing out a dead handle).
    backend.fail_releases = false;
    const auto reacquired = cache.acquire(key_for("first"), desc, pixels);
    REQUIRE(reacquired.ok());
    CHECK_FALSE(reacquired.cache_hit);
    CHECK(backend.uploads == 3);

    cache.clear();
    stats = cache.stats();
    CHECK(stats.resident_bytes == 0);
}

TEST_CASE("GpuAssetCache deduplicates concurrent population of the same key") {
    chronon3d::runtime::RenderSurfaceRegistry registry;
    CacheBackend backend;
    chronon3d::runtime::GpuAssetCache cache;
    cache.attach(registry, backend);

    const std::array<float, 4> pixels{0.25f, 0.5f, 0.75f, 1.0f};
    const chronon3d::runtime::SurfaceDesc desc{
        1, 1, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent,
        sizeof(pixels)};
    const auto key = key_for("contended-asset");
    // Force the uploads to overlap so the in-flight dedupe is actually
    // exercised rather than accidentally serialized by scheduling.
    backend.upload_delay_us = 2000;

    constexpr int kThreads = 8;
    std::vector<chronon3d::runtime::RenderSurfaceHandle> handles(kThreads,
        chronon3d::runtime::kInvalidRenderSurfaceHandle);
    std::vector<bool> ok(kThreads, false);
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i] {
            const auto result = cache.acquire(key, desc, pixels);
            ok[i] = result.ok();
            handles[i] = result.handle;
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    for (int i = 0; i < kThreads; ++i) {
        CHECK_MESSAGE(ok[i], "thread acquire must succeed");
        CHECK(handles[i] == handles[0]);
    }
    // Exactly one device upload happened even with 8 concurrent acquirers.
    CHECK(backend.uploads == 1);
    CHECK(registry.size() == 1);
}
