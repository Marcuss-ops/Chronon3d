#pragma once

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/text/mtsdf_profile.hpp>
#include <chronon3d/text/text_run_shape.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::assets { class AssetResolver; }

namespace chronon3d::media::video {

/// Prepared MTSDF atlas payload for the DirectYUV lowering. `gpu_rgba` keeps
/// one bit-preserving float cell per RGBA8 texel, exactly like the Vulkan
/// paged atlas transport. The CUDA compositor decodes the field at sample
/// time; no Blend2D or CPU RGBA raster is involved.
struct RasterizedTextTexture {
    int width{0};
    int height{0};
    float display_width{0.0f};
    float display_height{0.0f};
    float distance_range_px{
        static_cast<float>(::chronon3d::text::kCanonicalMtsdfProfile.distance_range_px)};
    bool mtsdf{false};
    std::vector<float> gpu_rgba;
    assets::ContentDigest digest{};
};

RasterizedTextTexture rasterize_text_texture(
    const TextRunShape& prepared_shape,
    const assets::AssetResolver& resolver);

/// Daemon-lifetime cache for the CPU preparation step of DirectYUV text.
///
/// The cache stores only immutable CPU MTSDF payloads. CUDA residency remains
/// owned by VideoDeviceRuntime::get_or_upload_image(), so this cache never
/// owns a CUDA context or a device allocation.  The layout fingerprint is the
/// same canonical identity used by the Vulkan prepared-text cache; paint and
/// animation bindings are deliberately excluded because they are shader-side
/// parameters and do not change the glyph atlas.
class DirectYuvTextTextureCache final {
public:
    static constexpr std::size_t kCapacityBytes = 64u * 1024u * 1024u;
    static constexpr std::size_t kShardCount = 16u;

    DirectYuvTextTextureCache()
        : entries_(kCapacityBytes, kShardCount, cache::CapacityMode::Weight) {}

    /// Resolver-aware lookup — the canonical path. The key mixes the layout
    /// fingerprint with the prepared-font content identity, so a font whose
    /// bytes change mid-daemon cannot be served from the previous packed
    /// texture.  The cache never retains the resolver.
    [[nodiscard]] std::shared_ptr<const RasterizedTextTexture> find(
        const TextRunLayout& layout,
        const assets::AssetResolver& resolver) const;

    /// LEGACY path-only lookup: keyed on `layout.layout_hash()` alone.  Kept for
    /// callers that hold no resolver (the fingerprint/reuse contract test); new
    /// production callers MUST use the resolver-aware overload so the packed
    /// lowering cannot bypass the content-addressed glyph identity.
    [[nodiscard]] std::shared_ptr<const RasterizedTextTexture> find(
        const TextRunLayout& layout) const;

    /// Resolver-aware store — the canonical path, matching find()/get_or_create().
    void store(const TextRunLayout& layout,
               const assets::AssetResolver& resolver,
               std::shared_ptr<const RasterizedTextTexture> texture);

    /// LEGACY path-only store: see the note on the path-only find().
    void store(const TextRunLayout& layout,
               std::shared_ptr<const RasterizedTextTexture> texture);

    /// Coalesces concurrent requests for one immutable layout. The loader is
    /// invoked once; other callers wait for the same prepared payload instead
    /// of rasterizing the same font run in parallel.
    [[nodiscard]] std::shared_ptr<const RasterizedTextTexture> get_or_create(
        const TextRunShape& prepared_shape,
        const assets::AssetResolver& resolver);

    void clear() noexcept { entries_.clear(); }

    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.stats().current_size;
    }

    [[nodiscard]] cache::LruCache<
        std::uint64_t,
        std::shared_ptr<const RasterizedTextTexture>>::Stats stats() const {
        return entries_.stats();
    }

private:
    /// Single key authority for find/store/get_or_create.  `resolver == nullptr`
    /// selects the legacy path-only fingerprint; a real resolver mixes the
    /// canonical prepared-font content identity into the key.  One function so
    /// the three entry points can never key differently again.
    [[nodiscard]] static std::uint64_t key_for(const TextRunLayout& layout,
                                               const assets::AssetResolver* resolver);

    using Cache = cache::LruCache<
        std::uint64_t,
        std::shared_ptr<const RasterizedTextTexture>>;
    // Lookups promote the entry and update hit/miss counters, so the
    // canonical LRU intentionally permits mutation through const find().
    mutable Cache entries_;
};

} // namespace chronon3d::media::video
