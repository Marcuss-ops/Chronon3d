#pragma once

#include <chronon3d/runtime/gpu_asset_cache.hpp>

#include <cstdint>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace chronon3d::runtime {

/// A rasterized glyph bitmap supplied by the text backend. Pixels are
/// premultiplied RGBA32F and remain owned by the caller.
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

/// Runtime-owned cache for packed text atlases. The cache key includes the
/// ordered glyph bitmap sequence, dimensions and pixels. This means a hit
/// skips both shelf packing and the CPU-side atlas copy; GpuAssetCache then
/// supplies the device-local image and its LRU ownership.
class GpuTextAtlasCache {
public:
    void attach(GpuAssetCache& assets) noexcept { m_assets = &assets; }

    [[nodiscard]] bool acquire(
        std::span<const PackedGlyphBitmap> glyphs,
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
                value ^= static_cast<std::size_t>(std::to_integer<unsigned char>(byte)) +
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

    GpuAssetCache* m_assets{nullptr};
    mutable std::mutex m_mutex;
    std::unordered_map<Key, Entry, KeyHash> m_entries;
};

} // namespace chronon3d::runtime
