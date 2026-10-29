#pragma once

// Bake-once / composite-many cache for `bake-layer`.
//
// A baked layer is a pure function of (layer content, canvas, output
// format, diagnostic flags, asset tree): the engine's own
// `cache::NodeCacheKey::digest()` of the selected layer node is the
// content-identity half of the key — the same authority the render-graph
// node cache uses to decide a framebuffer may be reused — and the remaining
// render-relevant parameters are folded in explicitly.
//
// On a hit the baked image is copied straight to the output, so repeated
// bakes of a static layer (e.g. one per video frame during a composite
// export) cost a file copy instead of a graph compile + execute. On a miss
// the image is rendered and then published into the cache with a
// write-temp-then-rename, so a concurrent reader never observes a partial
// entry.
//
// Staleness: the sorted relative paths, byte sizes, and per-file last-write
// times of the asset tree participate in the key. This is a metadata
// fingerprint, not a cryptographic content hash; the graph's NodeCacheKey
// remains the content identity authority. The walk is cheap next to a render.

#include <chrono>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace chronon3d::cli {

/// Everything that can change the pixels `bake-layer` writes.
struct BakeLayerCacheKeyInput {
    /// Engine content identity of the baked node
    /// (`RenderGraphNode::cache_key(ctx).digest()`); covers the frame and the
    /// node's params/source/input hashes.
    std::uint64_t node_content_digest{0};
    int width{0};
    int height{0};
    bool exr_bake{false};
    bool diagnostic_overlay{false};
    bool diagnostic_overlay_only{false};
    /// Defensive asset-input fingerprint (see assets_tree_fingerprint).
    std::uint64_t assets_fingerprint{0};
    /// Included only to keep distinct comp/layer requests apart when the node
    /// digest collides across scopes; not part of the content identity.
    std::string comp_id;
    std::string layer_id;
};

/// FNV-1a 64-bit, used to fold the key fields into one stable filename stem.
inline constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ULL;
inline constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;

inline void fnv1a64_mix(std::uint64_t& hash, const void* bytes, std::size_t size) {
    const auto* p = static_cast<const unsigned char*>(bytes);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(p[i]);
        hash *= kFnv1a64Prime;
    }
}

template <typename T>
inline void fnv1a64_mix_value(std::uint64_t& hash, T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    fnv1a64_mix(hash, &value, sizeof(T));
}

inline void fnv1a64_mix_string(std::uint64_t& hash, const std::string& value) {
    fnv1a64_mix_value(hash, value.size());
    fnv1a64_mix(hash, value.data(), value.size());
}

/// Stable fingerprint of an asset tree's relative paths and per-file metadata.
/// A missing root hashes as zero (nothing to invalidate against).
inline std::uint64_t assets_tree_fingerprint(const std::filesystem::path& root) {
    if (root.empty()) return 0;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return 0;

    std::uint64_t hash = kFnv1a64Offset;
    std::vector<std::tuple<std::string, std::uint64_t, std::uint64_t>> files;
    for (std::filesystem::recursive_directory_iterator it(
             root, std::filesystem::directory_options::skip_permission_denied, ec),
         end;
         it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const auto relative = std::filesystem::relative(it->path(), root, ec);
        if (ec) continue;
        const auto size = it->file_size(ec);
        if (ec) continue;
        const auto written = it->last_write_time(ec).time_since_epoch().count();
        if (ec) continue;
        files.emplace_back(relative.generic_string(),
                           static_cast<std::uint64_t>(size),
                           static_cast<std::uint64_t>(written));
    }
    std::sort(files.begin(), files.end());
    for (const auto& [path, size, written] : files) {
        fnv1a64_mix_string(hash, path);
        fnv1a64_mix_value(hash, size);
        fnv1a64_mix_value(hash, written);
    }
    return hash;
}

/// The canonical key: one hash over every field, order-tagged so no two
/// distinct inputs can fold into the same stem by re-partitioning bytes.
inline std::uint64_t bake_layer_cache_key(const BakeLayerCacheKeyInput& input) {
    std::uint64_t hash = kFnv1a64Offset;
    fnv1a64_mix_value(hash, input.node_content_digest);
    fnv1a64_mix_value(hash, input.width);
    fnv1a64_mix_value(hash, input.height);
    fnv1a64_mix_value(hash, static_cast<std::uint8_t>(input.exr_bake));
    fnv1a64_mix_value(hash, static_cast<std::uint8_t>(input.diagnostic_overlay));
    fnv1a64_mix_value(hash, static_cast<std::uint8_t>(input.diagnostic_overlay_only));
    fnv1a64_mix_value(hash, input.assets_fingerprint);
    fnv1a64_mix_string(hash, input.comp_id);
    fnv1a64_mix_string(hash, input.layer_id);
    return hash;
}

/// Cache entry filename: 16 hex digits of the key plus the image extension
/// the bake will write (the extension is cosmetic — the key already covers
/// the format flag — but keeps entries self-describing and loadable as-is).
inline std::string bake_layer_cache_filename(const BakeLayerCacheKeyInput& input) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::uint64_t key = bake_layer_cache_key(input);
    std::string stem(16, '0');
    for (int i = 15; i >= 0; --i) {
        stem[static_cast<std::size_t>(i)] = kHex[key & 0xF];
        key >>= 4;
    }
    return stem + (input.exr_bake ? ".exr" : ".png");
}

/// Filesystem store for bake entries. All paths live under one root; entries
/// are immutable once published (write-temp-then-rename).
class BakeLayerCache {
public:
    explicit BakeLayerCache(std::filesystem::path root) : root_(std::move(root)) {}

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

    /// Returns the cached entry when it exists, nullopt otherwise (miss).
    [[nodiscard]] std::optional<std::filesystem::path> lookup(
        const std::string& filename) const {
        std::error_code ec;
        const auto path = root_ / filename;
        if (std::filesystem::is_regular_file(path, ec)) return path;
        return std::nullopt;
    }

    /// Publishes `source` into the cache under `filename`. False on any
    /// filesystem error; the cache is then simply a miss for the next bake.
    bool store_from(const std::filesystem::path& source,
                    const std::string& filename) const {
        std::error_code ec;
        std::filesystem::create_directories(root_, ec);
        if (ec) return false;
        const auto destination = root_ / filename;
        // Write under a unique temporary name in the same directory, then
        // rename: readers see either the old entry or the complete new one.
        const auto temporary =
            destination.string() + ".tmp." + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::copy_file(
            source, temporary,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            std::filesystem::remove(temporary, ec);
            return false;
        }
        std::filesystem::rename(temporary, destination, ec);
        if (ec) {
            std::filesystem::remove(temporary, ec);
            return false;
        }
        return true;
    }

    /// Copies a cached entry to `destination` (the caller's output path).
    [[nodiscard]] bool materialize(const std::filesystem::path& entry,
                                   const std::filesystem::path& destination) const {
        std::error_code ec;
        std::filesystem::copy_file(
            entry, destination,
            std::filesystem::copy_options::overwrite_existing, ec);
        return !ec;
    }

private:
    std::filesystem::path root_;
};

} // namespace chronon3d::cli
