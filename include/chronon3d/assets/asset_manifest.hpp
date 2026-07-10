// ── include/chronon3d/assets/asset_manifest.hpp
//
// Phase A2 close-out #3/3 (2026-07-10) — FINAL canonical asset surface.
// SINGLE canonical home for the entire asset-manifest stack:
//
//   * `chronon3d::assets::AssetKind`        — enum Font/Image/Video/Audio
//                                             (POD, 4 values, type-erased
//                                             manifest owner-key).
//   * `chronon3d::assets::InternalAssetRef` — POD `{kind, path, owner,
//                                             required}`. Type-erased
//                                             internal storage slot.
//   * `chronon3d::AssetManifest`            — value-type with O(1)
//                                             `entry_for()` owner-keyed
//                                             lookup + `add_file/asset`
//                                             typed convenience methods
//                                             + filter/merge/clear.
//   * `chronon3d::assets::AssetManifest`    — `using` alias for the
//                                             flat-namespace spelling.
//
// Phase A2 lineage (post-A2 close-out):
//   * Phase A1 (2026-07-10) — removed the always-green
//     `chronon3d::assets::v2::AssetPreflightResult` stub +
//     `accumulate_preflight_result` bridge (compile-fail gate #18).
//   * Phase A2 #1/3 — flattened `chronon3d::assets::v2` to
//     `chronon3d::assets` flat namespace + renamed POD
//     `v2::AssetRef` -> `InternalAssetRef` + dropped `to_v2_ref()` +
//     added compile-fail migration gate #21.
//   * Phase A2 #2/3 — promoted `chronon3d::AssetManifest` to a SINGLE
//     canonical class body (top-level + flat-namespace alias), dropped
//     local `chronon3d::AssetRef` struct entirely, migrated 5 callers
//     from `AssetType::X` -> `assets::AssetKind::X`.
//   * Phase A2 #3/3 — INLINE PODs here from the deleted
//     `asset_readiness_v2.hpp` (single canonical home); DELETE the
//     orphan files (`asset_readiness_v2.hpp` +
//     `legacy_adapters.hpp`) + drop both from
//     `cmake/Chronon3DPublicHeaders.cmake`; fix P2 cosmetic drift
//     (banner count + alias style).
//
// Distinction from `chronon3d::AssetType` (in `asset_metadata.hpp`):
// `AssetType` is the 6-value registry/metadata discriminator (has
// `Mesh` + `Unknown` semantically). `assets::AssetKind` is the
// 4-value manifest/owner-key discriminator. The two coexist with
// distinct semantics — see `asset_metadata.hpp` for AssetType usage
// in `AssetRegistry::import_image/_font/_video/_audio` +
// `AssetMetadata::type`.
//
// Vincoli AGENTS.md v0.1 (Cat-3 freeze):
//   * ZERO nuovi singleton / registry / cache / service-locator.
//   * The manifest is a plain value type, NOT a singleton. Layer.hp and
//     scene.hpp each hold their own instance.
//
// Usage:
//   AssetManifest manifest;
//   manifest.add_font("assets/fonts/Inter-Bold.ttf", "title/text");
//   manifest.add_image("assets/bg.png", "background/rect");
//   for (const auto& ref : manifest.assets()) { /* ... */ }
//   (void)manifest.entry_for("title/text");   // std::optional<InternalAssetRef>;

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d::assets {

// ═══════════════════════════════════════════════════════════════════════════
// `AssetKind` — discriminator minimo per il `InternalAssetRef` del manifest.
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
// Type-erased POD storage slot. Phase A2 #1 renamed from the pre-A2
// `chronon3d::assets::v2::AssetRef`. Phase A2 #2 promoted this AS THE
// SINGLE canonical asset-ref shape after deleting the legacy
// `chronon3d::AssetRef` top-level struct.
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

} // namespace chronon3d::assets

namespace chronon3d {

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
    /// Type alias for entries. The canonical POD is
    /// `chronon3d::assets::InternalAssetRef` (Phase A2 inline).
    using Entry = assets::InternalAssetRef;

    AssetManifest() = default;

    // ── Generic add ────────────────────────────────────────────────────

    /// Insert an `InternalAssetRef`. First-inserted-wins on duplicate
    /// owner (the `m_by_owner.emplace` no-op semantics preserves the
    /// FIRST insertion; later duplicates are appended to `m_assets` and
    /// visible via `assets()` but NOT via `entry_for()`).
    void add(assets::InternalAssetRef ref) {
        m_by_owner.emplace(ref.owner, ref);
        m_assets.push_back(std::move(ref));
    }

    // ── Typed convenience methods ──────────────────────────────────────

    /// Add a font asset (owner-keyed; `AssetKind::Font`).
    void add_font(std::string path, std::string owner, bool required = true) {
        add({assets::AssetKind::Font, std::move(path), std::move(owner), required});
    }

    /// Add an image asset (owner-keyed; `AssetKind::Image`).
    void add_image(std::string path, std::string owner, bool required = true) {
        add({assets::AssetKind::Image, std::move(path), std::move(owner), required});
    }

    /// Add a video asset (owner-keyed; `AssetKind::Video`).
    void add_video(std::string path, std::string owner, bool required = true) {
        add({assets::AssetKind::Video, std::move(path), std::move(owner), required});
    }

    /// Add an audio asset (owner-keyed; `AssetKind::Audio`).
    void add_audio(std::string path, std::string owner, bool required = true) {
        add({assets::AssetKind::Audio, std::move(path), std::move(owner), required});
    }

    // ── O(1) owner-keyed lookup (Phase A2 #2/3 — promoted from v2 spec) ──

    /// First-inserted-wins lookup by `owner`. Returns `std::nullopt` if
    /// the owner has never been added (or if `clear()` was called).
    [[nodiscard]] std::optional<Entry>
    entry_for(const std::string& owner) const noexcept {
        const auto it = m_by_owner.find(owner);
        if (it == m_by_owner.end()) return std::nullopt;
        return it->second;
    }

    // ── Accessors ──────────────────────────────────────────────────────

    /// All entries in insertion order. Duplicates (same `owner` re-added
    /// after the first) appear in the result BY POSITION but the
    /// `m_by_owner` map keeps only the first one for `entry_for`.
    [[nodiscard]] const std::vector<assets::InternalAssetRef>& assets() const noexcept { return m_assets; }
    [[nodiscard]] std::vector<assets::InternalAssetRef>& assets() noexcept { return m_assets; }
    [[nodiscard]] bool empty() const noexcept { return m_assets.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_assets.size(); }

    // ── Merge another manifest into this one ───────────────────────────

    void merge(const AssetManifest& other) {
        for (const auto& ref : other.m_assets) {
            add(ref);  // first-inserted-wins via `m_by_owner.emplace`
        }
    }

    void merge(AssetManifest&& other) {
        for (auto& ref : other.m_assets) {
            add(std::move(ref));
        }
    }

    // ── Filter by asset kind ───────────────────────────────────────────

    /// Return only entries whose `kind` exactly matches `kind`.
    [[nodiscard]] std::vector<assets::InternalAssetRef>
    filter(assets::AssetKind kind) const {
        std::vector<assets::InternalAssetRef> result;
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
    std::vector<assets::InternalAssetRef> m_assets;
    std::unordered_map<std::string, assets::InternalAssetRef> m_by_owner;
};

} // namespace chronon3d

// ── Phase A2 #2/3 — flat-namespace alias ──────────────────────────────────
// `chronon3d::assets::AssetManifest` resolves to the same type as the
// top-level canonical `chronon3d::AssetManifest`. Both spellings
// return identical objects. The alias is the ONLY `AssetManifest`
// symbol declared in the `assets::` namespace of this header — there
// is no separate `assets::AssetManifest` class body here, so ABI
// / name-mangling is identical between the two spellings.
//
// P2 cosmetic (Phase A2 #3/3): dropped leading-`::` qualifier on
// the alias target so the syntax matches the rest of the codebase's
// `using` aliases.
namespace chronon3d::assets {
using AssetManifest = chronon3d::AssetManifest;
} // namespace chronon3d::assets
