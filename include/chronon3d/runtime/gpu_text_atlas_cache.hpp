#pragma once

#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/cache/lru_cache.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace chronon3d::runtime {

/// Immutable glyph bitmap input for the packed text atlas cache. Destination
/// position, opacity and highlight timing are deliberately excluded: they
/// belong to the per-frame GlyphInstance records.
struct PackedGlyphBitmap {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::span<const float> rgba{};
};

struct PackedTextAtlas {
    RenderSurfaceHandle handle{kInvalidRenderSurfaceHandle};
    std::uint32_t width{0};
    std::uint32_t height{0};
    bool cache_hit{false};
    bool uploaded{false};
    std::vector<std::uint32_t> origin_x;
    std::vector<std::uint32_t> origin_y;

    [[nodiscard]] bool valid() const noexcept {
        return handle != kInvalidRenderSurfaceHandle &&
               origin_x.size() == origin_y.size();
    }
};

/// Runtime-owned persistent packed atlas cache. The key is the ordered glyph
/// bitmap sequence, so equal immutable glyph pixels share one JobPersistent
/// GPU surface while transforms/opacity/highlight remain frame-dynamic.
class GpuTextAtlasCache {
public:
    static constexpr std::size_t kCapacityBytes = 512ULL * 1024ULL * 1024ULL;
    static constexpr std::size_t kShardCount = 16;

    GpuTextAtlasCache()
        : m_entries(kCapacityBytes, kShardCount) {}

    void attach(GpuAssetCache& assets) noexcept { m_assets = &assets; }

    [[nodiscard]] bool acquire(std::span<const PackedGlyphBitmap> glyphs,
                               PackedTextAtlas& out);

    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Key {
        assets::ContentDigest digest{};
        friend bool operator==(const Key&, const Key&) = default;
    };

    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept {
            std::size_t value = 0;
            for (const auto byte : key.digest.bytes) {
                value ^= static_cast<std::size_t>(
                    std::to_integer<unsigned char>(byte)) +
                    static_cast<std::size_t>(0x9e3779b9u) +
                    (value << 6u) + (value >> 2u);
            }
            return value;
        }
    };

    struct Entry {
        GpuAssetKey asset_key{};
        SurfaceDesc desc{};
        std::vector<float> atlas;
        PackedTextAtlas placement{};
    };

    using Cache = cache::LruCache<
        Key, std::shared_ptr<const Entry>, KeyHash>;

    GpuAssetCache* m_assets{nullptr};
    mutable Cache m_entries;
};

} // namespace chronon3d::runtime
