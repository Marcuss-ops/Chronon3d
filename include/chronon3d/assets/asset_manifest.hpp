// ── include/chronon3d/assets/asset_manifest.hpp
//
// Phase A2 close-out #2/3 (2026-07-10) — CANONICAL AssetManifest body (the
// single class implementation lives in the top-level `chronon3d::`
// namespace; the flat `chronon3d::assets::AssetManifest` is a `using` alias
// at the bottom).
//
// Designed to live in the top-level namespace to minimize external-API
// churn: `Layer::asset_manifest`, `Scene::m_manifest`, all CLI commands
// (`command_preflight.cpp`, `command_still.cpp`), all video exporters,
// `AssetPreflightResolver`, all tests, all content code
// (`sequence_v2_compositions.cpp`) ALREADY reference the top-level
// name `chronon3d::AssetManifest` and need to keep compiling.
//
// The flat-namespace spelling `chronon3d::assets::AssetManifest` is the
// canonical alias for downstream consumers that prefer the `assets::`
// namespace (e.g., `asset_ref.hpp` documents it). One identity, two
// spellings — no duplicate class body, no ODR violation.
//
// Entry type (Phase A2 rename, A2 #1/3): `assets::InternalAssetRef` POD
// (4 fields: `kind`=AssetKind, `path`, `owner`, `required`). The previous
// local `chronon3d::AssetRef` struct is DELETED in this commit (B1).
//
// Internally the manifest holds BOTH:
//   * `m_assets`    : vector in insertion order (preserved for `assets()`,
//                     and for `all()`-style semantics with first-inserted-
//                     wins duplicates kept visible).
//   * `m_by_owner`  : unordered_map keyed by `owner` for O(1)
//                     `entry_for(owner)` lookup.
// First-inserted-wins on duplicate owner keys (canonical semantics from
// the v2 spec adopted Phase-A2 #2: `std::unordered_map::emplace` on an
// existing key is a no-op from C++11, so the FIRST insertion is preserved).
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

#include <chronon3d/assets/asset_readiness_v2.hpp>   // AssetKind + InternalAssetRef
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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
    /// Type alias for entries. The canonical POD lives at
    /// `chronon3d::assets::InternalAssetRef` (Phase A2 rename).
    using Entry = assets::InternalAssetRef;

    AssetManifest() = default;

    // ── Generic add ────────────────────────────────────────────────────

    /// Insert an `InternalAssetRef` (the canonical entry type). First-inserted-
    /// wins on duplicate owner (the `m_by_owner.emplace` no-op semantics
    /// preserves the FIRST insertion; later duplicates are appended to
    /// `m_assets` and visible via `assets()` but not via `entry_for()`).
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
// symbol declared in this header's `assets::` namespace — there is no
// separate `assets::AssetManifest` class body here, so ABI / name-mangling
// is identical between the two spellings.
//
// Downstream consumers that include <chronon3d/assets/asset_ref.hpp>
// (which documents the canonical typed AssetRef<K> wrapper) may spell
// `assets::AssetManifest&` interchangeably with
// `chronon3d::AssetManifest&`.
namespace chronon3d::assets {
using AssetManifest = ::chronon3d::AssetManifest;
} // namespace chronon3d::assets
