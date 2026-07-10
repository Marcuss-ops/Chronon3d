// ═══════════════════════════════════════════════════════════════════════════
// asset_ref.hpp — canonical typed AssetRef<K> with compile-time asset kind.
//
// Phase A2 close-out (2026-07-10) — `v2::` references renamed to flat
// `assets::` namespace. The previously exposed v2 bridge methods
// (`to_v2_ref()`, `register_in()`, and the free-function factories
// `asset::image/font/video/audio`) have been removed because they were
// DEAD CODE: no production caller outside this file, despite being part
// of the public surface for a short while.
//
// AssetRef<K> is a value type that:
//
//   1.  Carries a typed asset kind at compile time (K = AssetKind::Image, …)
//   2.  Stores the raw path + owner + required flag (replacing scattered
//       raw `std::string path` locals)
//   3.  Resolves via AssetResolver (canonical engine-local path resolution)
//   4.  Imports into AssetRegistry (id-based lookup + dedup)
//
// The v2 `AssetManifest` (now `assets::AssetManifest` after Phase A2) is
// the canonical storage container, accessed via the flat
// `chronon3d::assets::AssetManifest` declaration in
// `asset_readiness_v2.hpp`. There is NO explicit `register_in()` method
// anymore; callers that need to deposit a typed asset into the manifest
// construct an `assets::InternalAssetRef` and pass it to
// `AssetManifest::add(...)` directly.
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/assets/asset_manifest.hpp>        // assets::AssetKind + assets::InternalAssetRef + assets::AssetManifest (Phase A2 #3/3)
#include <chronon3d/assets/asset_resolver.hpp>        // assets::AssetResolver
#include <chronon3d/assets/asset_registry.hpp>        // AssetRegistry, AssetId

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace chronon3d::assets {

// ── Canonical typed AssetRef<K> ──────────────────────────────────────────

/// Typed asset reference carrying compile-time AssetKind.
///
/// K is one of the four `assets::AssetKind` values (Image / Font / Video / Audio).
/// The template parameter enables:
///   - `import()` to call the correct `AssetRegistry::import_*()` method
///   - Type-safe factory functions (`AssetRef<Image>` is downstream-friendly)
///   - `if constexpr` dispatch in template code that consumes assets
///
/// AssetRef<K> is a pure value type — no hidden state, no singleton, no
/// service-locator.  It stores a path, an owner label, and a required flag.
template <assets::AssetKind K>
class AssetRef {
public:
    /// The compile-time asset kind for this reference.
    static constexpr assets::AssetKind kind = K;

    // ── Construction ──────────────────────────────────────────────────

    AssetRef() = default;

    /// Construct from path, optional owner label, and required flag.
    /// `required = true` (default) means preflight treats this as a
    /// hard dependency (missing = FAIL).
    explicit AssetRef(std::string path,
                      std::string owner    = "",
                      bool        required = true)
        : path_(std::move(path))
        , owner_(std::move(owner))
        , required_(required) {}

    // ── Accessors ─────────────────────────────────────────────────────

    /// The raw asset path (relative to assets root, or absolute).
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    /// Logical owner label (e.g. "hero/background", "title/text").
    [[nodiscard]] const std::string& owner() const noexcept { return owner_; }

    /// Whether this asset is a hard prerequisite for the scene.
    [[nodiscard]] bool required() const noexcept { return required_; }

    // ── Resolution (unifies AssetResolver) ────────────────────────────

    /// Resolve the path against `resolver` and check on-disk existence.
    /// Returns the resolved absolute path, or std::nullopt if the asset
    /// cannot be found (missing mount, clamp violation, etc.).
    [[nodiscard]] std::optional<std::filesystem::path>
    resolve(const AssetResolver& resolver) const {
        return resolver.resolve(path_);
    }

    /// Resolve the path against `resolver` WITHOUT touching the
    /// filesystem.  Faster than resolve() for hot paths (preflight,
    /// font lookup).  Same clamp + missing semantics except the
    /// on-disk existence check is skipped.
    [[nodiscard]] std::optional<std::filesystem::path>
    resolve_lexical(const AssetResolver& resolver) const {
        return resolver.resolve_lexical(path_);
    }

    // ── Import (unifies AssetRegistry) ────────────────────────────────

    /// Resolve the path and import into `registry`, returning the
    /// stable AssetId.  The resolver is used to canonicalize the path;
    /// if resolution fails, throws std::runtime_error.
    ///
    /// Compile-time dispatch on K calls the correct import_*() method
    /// (import_image / import_font / import_video / import_audio).
    [[nodiscard]] AssetId import(AssetRegistry&        registry,
                                 const AssetResolver& resolver) const {
        auto resolved = resolve(resolver);
        if (!resolved) {
            throw std::runtime_error(
                "AssetRef::import: cannot resolve path '" + path_ + "'");
        }
        if constexpr (K == assets::AssetKind::Image) {
            return registry.import_image(*resolved);
        } else if constexpr (K == assets::AssetKind::Font) {
            return registry.import_font(*resolved);
        } else if constexpr (K == assets::AssetKind::Video) {
            return registry.import_video(*resolved);
        } else if constexpr (K == assets::AssetKind::Audio) {
            return registry.import_audio(*resolved);
        } else {
            static_assert(K != K, "AssetRef::import: unknown AssetKind");
        }
    }

private:
    std::string path_;
    std::string owner_;
    bool        required_{true};
};

// ── Convenience type aliases ─────────────────────────────────────────────

using ImageRef = AssetRef<assets::AssetKind::Image>;
using FontRef  = AssetRef<assets::AssetKind::Font>;
using VideoRef = AssetRef<assets::AssetKind::Video>;
using AudioRef = AssetRef<assets::AssetKind::Audio>;

} // namespace chronon3d::assets
