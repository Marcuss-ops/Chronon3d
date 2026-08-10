#pragma once

#include <chronon3d/assets/asset_manifest.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/math/color.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::assets {

/// Stable preparation identity for a resolved mesh asset.
/// The content digest is authoritative; path/size/time are retained for
/// diagnostics and cheap identity inspection.
struct MeshIdentity {
    std::string resolved_path;
    std::uint64_t byte_size{0};
    std::int64_t write_time{0};
    ContentDigest content_digest{};

    [[nodiscard]] std::string cache_key() const;
    friend bool operator==(const MeshIdentity&, const MeshIdentity&) = default;
};

enum class MeshLoadErrorCode : std::uint8_t {
    MissingAsset = 0,
    ReadFailed = 1,
    InvalidGlb = 2,
    UnsupportedGlb = 3,
    InvalidGeometry = 4,
    InvalidReference = 5,
};

struct MeshLoadError {
    MeshLoadErrorCode code{MeshLoadErrorCode::InvalidGlb};
    std::string path;
    std::string message;
};

/// One prepared primitive from a GLB mesh.
struct MeshPart {
    std::string name;
    std::shared_ptr<const Mesh> geometry;
    /// Null when the primitive has no material; avoids implying material 0.
    std::optional<std::uint32_t> material_index;
};

/// Prepared embedded image bytes. The loader owns the bytes and exposes no
/// glTF image/view types; downstream preparation can decode this payload using
/// the canonical Chronon image services.
struct MeshImage {
    std::string mime_type;
    std::vector<std::byte> payload;
};

/// V1 base material converted from glTF PBR data into Chronon-native types.
/// `base_color_texture_index`, when present, indexes PreparedMeshSource::images.
struct MeshMaterial {
    std::string name;
    Color base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
    std::optional<std::uint32_t> base_color_texture_index;
};

/// Immutable geometry produced at the AssetResolver → MeshLoader boundary.
/// No filesystem work is required after this object is prepared.
struct PreparedMeshSource {
    std::string logical_path;
    std::filesystem::path resolved_path;
    MeshIdentity identity;
    std::vector<MeshPart> parts;
    std::vector<MeshMaterial> materials;
    std::vector<MeshImage> images;
};

using PreparedMeshSourceRef = std::shared_ptr<const PreparedMeshSource>;

/// Runtime-owned cache for prepared meshes. Entries are keyed by the full
/// resolved asset identity, never by a process-global logical path.
class MeshPreparationCache {
public:
    explicit MeshPreparationCache(std::size_t capacity = 64)
        : m_cache(capacity, 1, cache::CapacityMode::Count) {}

    MeshPreparationCache(const MeshPreparationCache&) = delete;
    MeshPreparationCache& operator=(const MeshPreparationCache&) = delete;
    MeshPreparationCache(MeshPreparationCache&&) noexcept = default;
    MeshPreparationCache& operator=(MeshPreparationCache&&) noexcept = default;

    [[nodiscard]] std::optional<PreparedMeshSourceRef> find(
        const MeshIdentity& identity) {
        return m_cache.get(identity.cache_key());
    }

    void store(const MeshIdentity& identity, PreparedMeshSourceRef source) {
        m_cache.put(identity.cache_key(), std::move(source));
    }

    [[nodiscard]] std::size_t size() const { return m_cache.stats().current_size; }
    [[nodiscard]] cache::LruCache<std::string, PreparedMeshSourceRef>::Stats stats() const {
        return m_cache.stats();
    }

private:
    cache::LruCache<std::string, PreparedMeshSourceRef> m_cache;
};

/// GLB-only importer. It accepts logical references and resolves them through
/// the supplied engine-local AssetResolver; it never owns a resolver or
/// accesses an asset root independently.
class MeshLoader {
public:
    [[nodiscard]] static Result<PreparedMeshSourceRef, MeshLoadError> load(
        const InternalAssetRef& ref,
        const AssetResolver& resolver,
        MeshPreparationCache* cache = nullptr);
};

} // namespace chronon3d::assets
