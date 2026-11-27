#include <doctest/doctest.h>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/backends/image/image_backend.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <semaphore>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

namespace {

class CacheBackend final : public chronon3d::graph::RenderBackend {
public:
    chronon3d::graph::RenderOpResult create_surface(
        chronon3d::runtime::RenderSurfaceHandle handle,
        const chronon3d::runtime::SurfaceDesc&) override {
        const auto index = created_count.fetch_add(1, std::memory_order_relaxed);
        if (index < created.size()) {
            created[index].store(handle, std::memory_order_relaxed);
        }
        return chronon3d::graph::RenderOpResult(chronon3d::graph::RenderOpOutcome{});
    }

    chronon3d::graph::RenderOpResult release_surface(
        chronon3d::runtime::RenderSurfaceHandle handle) override {
        const auto released_index = released_count.fetch_add(1, std::memory_order_relaxed);
        if (released_index < released.size()) {
            released[released_index].store(handle, std::memory_order_relaxed);
        }
        auto expected = handle;
        if (block_release_handle.compare_exchange_strong(
                expected, chronon3d::runtime::kInvalidRenderSurfaceHandle,
                std::memory_order_acq_rel)) {
            release_entered.release();
            release_continue.acquire();
        }
        if (fail_releases) {
            return chronon3d::graph::RenderOpResult(chronon3d::graph::make_render_backend_error(
                chronon3d::graph::RenderBackendErrorCode::ExecutionFailure,
                "release_surface: simulated device failure"));
        }
        return chronon3d::graph::RenderOpResult(chronon3d::graph::RenderOpOutcome{});
    }

    bool is_native_surface_valid(
        chronon3d::runtime::RenderSurfaceHandle handle) const noexcept override {
        const auto contains = [handle](const auto& values, const auto& count) {
            const auto size = std::min(count.load(std::memory_order_relaxed), values.size());
            for (std::size_t index = 0; index < size; ++index) {
                if (values[index].load(std::memory_order_relaxed) == handle) return true;
            }
            return false;
        };
        return handle != chronon3d::runtime::kInvalidRenderSurfaceHandle &&
               contains(created, created_count) &&
               !contains(released, released_count) &&
               !contains(invalidated, invalidated_count);
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
        const auto index = invalidated_count.fetch_add(1, std::memory_order_relaxed);
        if (index < invalidated.size()) {
            invalidated[index].store(handle, std::memory_order_relaxed);
        }
    }

    void block_release(chronon3d::runtime::RenderSurfaceHandle handle) {
        block_release_handle.store(handle, std::memory_order_release);
    }

    bool wait_for_blocked_release() {
        return release_entered.try_acquire_for(std::chrono::seconds(5));
    }

    void continue_release() {
        release_continue.release();
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

    std::array<std::atomic<chronon3d::runtime::RenderSurfaceHandle>, 2048> created{};
    std::atomic<std::size_t> created_count{0};
    std::array<std::atomic<chronon3d::runtime::RenderSurfaceHandle>, 2048> released{};
    std::atomic<std::size_t> released_count{0};
    std::array<std::atomic<chronon3d::runtime::RenderSurfaceHandle>, 2048> invalidated{};
    std::atomic<std::size_t> invalidated_count{0};
    std::atomic<std::size_t> uploads{0};
    std::atomic<bool> fail_releases{false};
    std::uint32_t upload_delay_us{0};
    std::atomic<chronon3d::runtime::RenderSurfaceHandle> block_release_handle{
        chronon3d::runtime::kInvalidRenderSurfaceHandle};
    std::counting_semaphore<1> release_entered{0};
    std::counting_semaphore<1> release_continue{0};
};

class SyntheticImageBackend final : public chronon3d::image::ImageBackend {
public:
    std::unique_ptr<chronon3d::image::ImageBuffer> load_image(
        const std::string& path) override {
        loads.fetch_add(1, std::memory_order_relaxed);
        auto image = std::make_unique<chronon3d::image::ImageBuffer>();
        image->width = 128;
        image->height = 256;
        image->channels = 4;
        image->pixels = std::make_unique<chronon3d::u8[]>(128 * 256 * 4);
        const auto tint = static_cast<chronon3d::u8>(
            std::hash<std::string>{}(path) & 0xffU);
        for (int i = 0; i < 128 * 256; ++i) {
            image->pixels[i * 4 + 0] = tint;
            image->pixels[i * 4 + 1] = 96;
            image->pixels[i * 4 + 2] = 192;
            image->pixels[i * 4 + 3] = 255;
        }
        return image;
    }

    std::atomic<std::size_t> loads{0};
};

class CapturingSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    std::vector<spdlog::level::level_enum> levels() {
        std::lock_guard lock(mutex_);
        return levels_;
    }

protected:
    void sink_it_(const spdlog::details::log_msg& message) override {
        levels_.push_back(message.level);
    }
    void flush_() override {}

private:
    std::vector<spdlog::level::level_enum> levels_;
};

class DefaultLoggerGuard {
public:
    explicit DefaultLoggerGuard(const std::shared_ptr<spdlog::logger>& replacement)
        : previous_(spdlog::default_logger()) {
        spdlog::set_default_logger(replacement);
    }
    ~DefaultLoggerGuard() { spdlog::set_default_logger(previous_); }

private:
    std::shared_ptr<spdlog::logger> previous_;
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

TEST_CASE("GpuAssetKey identity includes content and upload representation") {
    chronon3d::runtime::GpuAssetKey base{
        chronon3d::assets::sha256_string("texture-v1"),
        chronon3d::runtime::PixelFormat::Rgba32Float,
        128,
        64};
    const auto original = chronon3d::runtime::GpuAssetKeyHash{}(base);
    const auto changed = [&](auto mutate) {
        auto key = base;
        mutate(key);
        CHECK(chronon3d::runtime::GpuAssetKeyHash{}(key) != original);
    };
    changed([](auto& key) { key.content_digest = chronon3d::assets::sha256_string("texture-v2"); });
    changed([](auto& key) { key.format = chronon3d::runtime::PixelFormat::Nv12; });
    changed([](auto& key) { ++key.width; });
    changed([](auto& key) { ++key.height; });
}

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
    // vector<bool> packs logical elements into shared machine words; concurrent
    // writes to distinct indexes can still race on the same word.
    std::vector<std::uint8_t> ok(kThreads, 0);
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

TEST_CASE("GpuAssetCache stress preserves accounting across concurrent unique keys") {
    constexpr std::size_t kUniqueKeys = 1000;
    constexpr std::size_t kThreadCount = 8;
    const std::array<float, 4> pixels{0.25f, 0.5f, 0.75f, 1.0f};
    const chronon3d::runtime::SurfaceDesc desc{
        1, 1, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, sizeof(pixels)};
    std::vector<chronon3d::runtime::GpuAssetKey> keys;
    keys.reserve(kUniqueKeys);
    for (std::size_t i = 0; i < kUniqueKeys; ++i) {
        keys.push_back(key_for("stress-asset-" + std::to_string(i)));
    }

    chronon3d::runtime::RenderSurfaceRegistry registry;
    CacheBackend backend;
    chronon3d::runtime::GpuAssetCache cache(sizeof(pixels) * 2);
    cache.attach(registry, backend);
    std::atomic<bool> all_acquires_ok{true};
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);
    for (std::size_t worker = 0; worker < kThreadCount; ++worker) {
        threads.emplace_back([&, worker] {
            for (std::size_t index = worker; index < keys.size(); index += kThreadCount) {
                if (!cache.acquire(keys[index], desc, pixels).ok()) {
                    all_acquires_ok.store(false, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& thread : threads) thread.join();

    CHECK(all_acquires_ok.load(std::memory_order_relaxed));
    CHECK(backend.uploads == kUniqueKeys);
    CHECK(cache.stats().initial_uploads == kUniqueKeys);
    CHECK(cache.stats().resident_bytes <= sizeof(pixels) * 2);
    CHECK(registry.size() <= 2);
    cache.clear();
    CHECK(cache.stats().resident_bytes == 0);
    CHECK(cache.stats().release_failures == 0);
    CHECK(registry.size() == 0);
}

TEST_CASE("GpuAssetCache reserves an evicted key while its surface is released") {
    chronon3d::runtime::RenderSurfaceRegistry registry;
    CacheBackend backend;
    chronon3d::runtime::GpuAssetCache cache(sizeof(float) * 4);
    cache.attach(registry, backend);

    const std::array<float, 4> pixels{0.25f, 0.5f, 0.75f, 1.0f};
    const chronon3d::runtime::SurfaceDesc desc{
        1, 1, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, sizeof(pixels)};
    const auto first_key = key_for("evicted-key-reservation");
    REQUIRE(cache.acquire(first_key, desc, pixels).ok());
    const auto first_handle = cache.acquire(first_key, desc, pixels).handle;

    backend.block_release(first_handle);
    std::atomic<bool> replacement_ok{false};
    std::thread evictor([&] {
        replacement_ok.store(cache.acquire(key_for("evictor"), desc, pixels).ok(),
                             std::memory_order_release);
    });
    const bool release_blocked = backend.wait_for_blocked_release();
    CHECK(release_blocked);
    if (!release_blocked) {
        backend.continue_release();
        evictor.join();
        return;
    }

    std::atomic<bool> reacquire_done{false};
    std::atomic<bool> reacquire_ok{false};
    std::thread reacquirer([&] {
        reacquire_ok.store(cache.acquire(first_key, desc, pixels).ok(),
                           std::memory_order_release);
        reacquire_done.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK_FALSE(reacquire_done.load(std::memory_order_acquire));
    // The released entry is reserved as in-flight, so no duplicate upload can
    // begin while the backend still owns its old physical surface.
    CHECK(backend.uploads == 2);

    backend.continue_release();
    evictor.join();
    reacquirer.join();
    CHECK(replacement_ok.load(std::memory_order_acquire));
    CHECK(reacquire_ok.load(std::memory_order_acquire));
    CHECK(backend.uploads == 3);
    CHECK(registry.size() == 1);
    CHECK(cache.stats().resident_bytes == sizeof(pixels));
}

TEST_CASE("GpuAssetCache clear is a barrier for concurrent acquire") {
    chronon3d::runtime::RenderSurfaceRegistry registry;
    CacheBackend backend;
    chronon3d::runtime::GpuAssetCache cache;
    cache.attach(registry, backend);

    const std::array<float, 4> pixels{0.25f, 0.5f, 0.75f, 1.0f};
    const chronon3d::runtime::SurfaceDesc desc{
        1, 1, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, sizeof(pixels)};
    const auto key = key_for("clear-barrier");
    const auto initial = cache.acquire(key, desc, pixels);
    REQUIRE(initial.ok());
    backend.block_release(initial.handle);

    std::thread clearer([&] { cache.clear(); });
    const bool release_blocked = backend.wait_for_blocked_release();
    CHECK(release_blocked);
    if (!release_blocked) {
        backend.continue_release();
        clearer.join();
        return;
    }
    std::atomic<bool> acquire_done{false};
    std::atomic<bool> acquire_ok{false};
    std::thread acquirer([&] {
        acquire_ok.store(cache.acquire(key, desc, pixels).ok(),
                         std::memory_order_release);
        acquire_done.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK_FALSE(acquire_done.load(std::memory_order_acquire));
    CHECK(backend.uploads == 1);

    backend.continue_release();
    clearer.join();
    acquirer.join();
    CHECK(acquire_ok.load(std::memory_order_acquire));
    CHECK(backend.uploads == 2);
    CHECK(registry.size() == 1);
    CHECK(cache.stats().resident_bytes == sizeof(pixels));
}

TEST_CASE("ImageCache accounting includes framebuffer padding and vector capacity") {
    chronon3d::CachedImage image;
    image.width = 5;
    image.height = 3;
    image.fb_img = std::make_shared<chronon3d::Framebuffer>(image.width, image.height);
    image.gpu_rgba.reserve(256);
    image.gpu_rgba.resize(static_cast<std::size_t>(image.width) * image.height * 4);

    std::size_t expected = image.fb_img->size_bytes() +
                           image.gpu_rgba.capacity() * sizeof(float);
#ifdef CHRONON3D_USE_BLEND2D
    REQUIRE(image.bl_img.create(image.width, image.height, BL_FORMAT_PRGB32) == BL_SUCCESS);
    BLImageData bl_data;
    REQUIRE(image.bl_img.getData(&bl_data) == BL_SUCCESS);
    REQUIRE(bl_data.pixelData != nullptr);
    expected += static_cast<std::size_t>(std::abs(bl_data.stride)) * image.height;
#endif
    CHECK(chronon3d::image_cache_detail::cached_image_bytes(image) == expected);
    CHECK(image.gpu_rgba.capacity() > image.gpu_rgba.size());

    // Empty optional Blend2D storage exercises the same accounting path as a
    // build without Blend2D (and float/linear inputs in a build with it).
    chronon3d::CachedImage without_blend2d;
    without_blend2d.width = 5;
    without_blend2d.height = 3;
    without_blend2d.fb_img = std::make_shared<chronon3d::Framebuffer>(5, 3);
    without_blend2d.gpu_rgba.reserve(128);
    without_blend2d.gpu_rgba.resize(5 * 3 * 4);
    CHECK(chronon3d::image_cache_detail::cached_image_bytes(without_blend2d) ==
          without_blend2d.fb_img->size_bytes() +
              without_blend2d.gpu_rgba.capacity() * sizeof(float));
}

TEST_CASE("ImageCache admission uses the accounted allocation size as its budget") {
    auto sizing_backend = std::make_shared<SyntheticImageBackend>();
    chronon3d::ImageCache sizing_cache(4 * 1024 * 1024);
    sizing_cache.set_backend(sizing_backend);
    const auto sample = sizing_cache.get_or_load("budget-sample.png");
    REQUIRE(sample != nullptr);
    REQUIRE(sample->valid());
    const auto allocation =
        chronon3d::image_cache_detail::cached_image_bytes(*sample);
    REQUIRE(allocation > 1);

    auto exact_backend = std::make_shared<SyntheticImageBackend>();
    chronon3d::ImageCache exact_cache(allocation * 2);
    exact_cache.set_backend(exact_backend);
    REQUIRE(exact_cache.get_or_load("exact-budget.png") != nullptr);
    CHECK(exact_cache.size() == 1);
    CHECK(exact_cache.find("exact-budget.png") != nullptr);

    auto undersized_backend = std::make_shared<SyntheticImageBackend>();
    chronon3d::ImageCache undersized_cache(allocation * 2 - 2);
    undersized_cache.set_backend(undersized_backend);
    const auto decoded = undersized_cache.get_or_load("under-budget.png");
    REQUIRE(decoded != nullptr);
    REQUIRE(decoded->valid());
    CHECK(undersized_cache.size() == 0);
    CHECK(undersized_cache.find("under-budget.png") == nullptr);
}

TEST_CASE("Image decode references survive one-entry cache eviction through GPU promotion") {
    constexpr std::size_t kImageCount = 10;
    auto sizing_backend = std::make_shared<SyntheticImageBackend>();
    chronon3d::ImageCache sizing_cache(1024 * 1024);
    sizing_cache.set_backend(sizing_backend);
    const auto sized = sizing_cache.get_or_load("size-profile.png");
    REQUIRE(sized != nullptr);
    REQUIRE(sized->valid());
    const auto one_image_bytes =
        chronon3d::image_cache_detail::cached_image_bytes(*sized);
    REQUIRE(one_image_bytes > 0);

    auto decode_backend = std::make_shared<SyntheticImageBackend>();
    chronon3d::ImageCache image_cache(1024 * 1024);
    image_cache.set_backend(decode_backend);
    std::vector<std::shared_ptr<const chronon3d::CachedImage>> decoded_tasks;
    decoded_tasks.reserve(kImageCount);
    for (std::size_t i = 0; i < kImageCount; ++i) {
        auto image = image_cache.get_or_load("asset-" + std::to_string(i) + ".png");
        REQUIRE(image != nullptr);
        REQUIRE(image->valid());
        decoded_tasks.push_back(std::move(image));
    }

    // A 1 MiB sharded cache gives each shard 64 KiB, so these ~640 KiB
    // decoded images are deliberately not retained by any shard.
    CHECK(image_cache.size() == 0);
    CHECK(image_cache.find("asset-0.png") == nullptr);
    CHECK(decode_backend->loads == kImageCount);

    chronon3d::runtime::RenderSurfaceRegistry registry;
    CacheBackend gpu_backend;
    chronon3d::runtime::GpuAssetCache gpu_cache;
    gpu_cache.attach(registry, gpu_backend);
    for (const auto& image : decoded_tasks) {
        REQUIRE(image != nullptr);
        REQUIRE(image->valid());
        const auto pixels = image->gpu_rgba_span();
        REQUIRE_FALSE(pixels.empty());
        const auto& key = image->gpu_key;
        const chronon3d::runtime::SurfaceDesc desc{
            key.width, key.height, key.format,
            chronon3d::runtime::ResourceUsage::Storage,
            chronon3d::runtime::LifetimeClass::JobPersistent, pixels.size_bytes()};
        CHECK(gpu_cache.acquire(key, desc, pixels).ok());
    }
    CHECK(gpu_backend.uploads == kImageCount);
    CHECK(decode_backend->loads == kImageCount);
    CHECK(gpu_cache.stats().initial_uploads == kImageCount);
    gpu_cache.clear();
    CHECK(registry.size() == 0);
}

TEST_CASE("ImageCache hot-path events are not logged at info level") {
    auto sink = std::make_shared<CapturingSink>();
    auto logger = std::make_shared<spdlog::logger>("image-cache-test", sink);
    logger->set_level(spdlog::level::trace);
    DefaultLoggerGuard logger_guard(logger);

    chronon3d::ImageCache cache(1024 * 1024);
    cache.set_backend(std::make_shared<SyntheticImageBackend>());
    REQUIRE(cache.get_or_load("log-full.png") != nullptr);
    chronon3d::ImageDecodeOptions bounded;
    bounded.max_dimension = 8;
    REQUIRE(cache.get_or_load("log-downscaled.png", bounded) != nullptr);

    const auto levels = sink->levels();
    CHECK(std::count(levels.begin(), levels.end(), spdlog::level::info) == 0);
    CHECK(std::count(levels.begin(), levels.end(), spdlog::level::debug) >= 3);
}
