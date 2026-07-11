#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d::assets {

// ═══════════════════════════════════════════════════════════════════════════
// `AssetKind` — discriminator for the type-erased `InternalAssetRef`.
//
// 4 valori, sizeof 1 byte. Distinto da `chronon3d::AssetType` (6 valori
// inclusi Mesh + Unknown, semantica metadata/registry; vive in
// `asset_metadata.hpp`, usato da `AssetRegistry::import_*` e
// `AssetMetadata::type`).
// ═══════════════════════════════════════════════════════════════════════════
enum class AssetKind : unsigned char {
    Font  = 0,
    Image = 1,
    Video = 2,
    Audio = 3
};

// ═══════════════════════════════════════════════════════════════════════════
// `InternalAssetRef` — reference canonico a un asset richiesto da una scena.
//
// Type-erased POD storage slot.
//
//   * `kind`     : `AssetKind` (Font/Image/Video/Audio).
//   * `path`     : path raw (relativo a `assets_root` o assoluto).
//   * `owner`    : identificatore logico del "proprietario" dell'asset
//                  (es. "LightPulse/text/label", "CenterTextOptions/label",
//                  "SceneBuilder::image(\"hero\")"). Discriminant per
//                  `AssetManifest::entry_for(owner)`. Unico per scena.
//   * `required` : se true, l'asset è HARD-REQUIRED (mancanza = FAIL
//                  preflight con `ok=false` + `missing` non-empty);
//                  se false, l'asset è soft-optional.
// ═══════════════════════════════════════════════════════════════════════════
struct InternalAssetRef {
    AssetKind  kind{AssetKind::Image};
    std::string path{};
    std::string owner{};
    bool        required{true};
};

// ═══════════════════════════════════════════════════════════════════════════
// `AssetManifest` — collects asset references during build
// ═══════════════════════════════════════════════════════════════════════════
//
// Populated by LayerBuilder as assets are referenced. Aggregated by
// SceneBuilder across all layers. Consumed by AssetPreflightResolver.
//
// Storage: vector + unordered_map (both updated on every `add()` call;
// the map is the O(1) lookup table for `entry_for(owner)`; the vector
// preserves insertion order and keeps duplicates visible to `assets()`).
//
class AssetManifest {
public:
    using Entry = InternalAssetRef;

    AssetManifest() = default;

    /// Insert an `InternalAssetRef`. First-inserted-wins on duplicate
    /// owner (the `m_by_owner.emplace` no-op semantics preserves the
    /// FIRST insertion; later duplicates are appended to `m_assets` and
    /// visible via `assets()` but NOT via `entry_for()`).
    void add(InternalAssetRef ref) {
        m_by_owner.emplace(ref.owner, ref);
        m_assets.push_back(std::move(ref));
    }

    /// Add a font asset (owner-keyed; `AssetKind::Font`).
    void add_font(std::string path, std::string owner, bool required = true) {
        add({AssetKind::Font, std::move(path), std::move(owner), required});
    }

    /// Add an image asset (owner-keyed; `AssetKind::Image`).
    void add_image(std::string path, std::string owner, bool required = true) {
        add({AssetKind::Image, std::move(path), std::move(owner), required});
    }

    /// Add a video asset (owner-keyed; `AssetKind::Video`).
    void add_video(std::string path, std::string owner, bool required = true) {
        add({AssetKind::Video, std::move(path), std::move(owner), required});
    }

    /// Add an audio asset (owner-keyed; `AssetKind::Audio`).
    void add_audio(std::string path, std::string owner, bool required = true) {
        add({AssetKind::Audio, std::move(path), std::move(owner), required});
    }

    /// First-inserted-wins lookup by `owner`. Returns `std::nullopt` if
    /// the owner has never been added (or if `clear()` was called).
    [[nodiscard]] std::optional<Entry>
    entry_for(const std::string& owner) const noexcept {
        const auto it = m_by_owner.find(owner);
        if (it == m_by_owner.end()) return std::nullopt;
        return it->second;
    }

    /// All entries in insertion order. Duplicates (same `owner` re-added
    /// after the first) appear in the result BY POSITION but the
    /// `m_by_owner` map keeps only the first one for `entry_for`.
    [[nodiscard]] const std::vector<InternalAssetRef>& assets() const noexcept { return m_assets; }
    [[nodiscard]] std::vector<InternalAssetRef>& assets() noexcept { return m_assets; }
    [[nodiscard]] bool empty() const noexcept { return m_assets.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_assets.size(); }

    /// Merge another manifest into this one.
    void merge(const AssetManifest& other) {
        for (const auto& ref : other.m_assets) {
            add(ref);
        }
    }

    void merge(AssetManifest&& other) {
        for (auto& ref : other.m_assets) {
            add(std::move(ref));
        }
    }

    /// Return only entries whose `kind` exactly matches `kind`.
    [[nodiscard]] std::vector<InternalAssetRef>
    filter(AssetKind kind) const {
        std::vector<InternalAssetRef> result;
        for (const auto& ref : m_assets) {
            if (ref.kind == kind) result.push_back(ref);
        }
        return result;
    }

    /// Drop all entries; both the insertion-order vector and the owner map
    /// are reset to empty.
    void clear() {
        m_assets.clear();
        m_by_owner.clear();
    }

private:
    std::vector<InternalAssetRef> m_assets;
    std::unordered_map<std::string, InternalAssetRef> m_by_owner;
};

} // namespace chronon3d::assets
