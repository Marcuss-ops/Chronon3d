// ═══════════════════════════════════════════════════════════════════════════
// asset_ref.hpp — canonical typed AssetRef<K> with compile-time asset kind.
//
// B3 — replaces scattered raw paths, direct AssetRegistry/AssetResolver
// calls with a single unified typed interface.  AssetRef<K> is a value
// type that:
//
//   1.  Carries a typed asset kind at compile time (K = AssetKind::Image, …)
//   2.  Stores the raw path + owner + required flag (replacing scattered
//       raw `std::string path` locals)
//   3.  Resolves via AssetResolver (canonical engine-local path resolution)
//   4.  Imports into AssetRegistry (id-based lookup + dedup)
//   5.  Registers into v2::AssetManifest (preflight asset readiness)
//
// Free function factories in `chronon3d::asset` (lowercase, user-facing):
//
//     auto bg  = asset::image("bg.png", "hero/background");
//     auto fnt = asset::font("fonts/Inter.ttf", "title/text");
//     auto vid = asset::video("intro.mp4", "opening/sequence");
//
// With optional auto-registration:
//
//     v2::AssetManifest manifest;
//     auto bg = asset::image("bg.png", "hero/bg", true, &manifest);
//     // bg is now in manifest AND returned as AssetRef<Image>
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/assets/asset_readiness_v2.hpp>   // v2::AssetKind, v2::AssetRef, v2::AssetManifest
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
/// K is one of the four `v2::AssetKind` values (Image / Font / Video / Audio).
/// The template parameter enables:
///   - `import()` to call the correct `AssetRegistry::import_*()` method
///   - Type-safe factory functions (`asset::image()` returns `ImageRef`)
///   - `if constexpr` dispatch in template code that consumes assets
///
/// AssetRef<K> is a pure value type — no hidden state, no singleton, no
/// service-locator.  It stores a path, an owner label, and a required flag.
template <v2::AssetKind K>
class AssetRef {
public:
    /// The compile-time asset kind for this reference.
    static constexpr v2::AssetKind kind = K;

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
    /// Used by v2::AssetManifest::entry_for() for O(1) lookup.
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
        if constexpr (K == v2::AssetKind::Image) {
            return registry.import_image(*resolved);
        } else if constexpr (K == v2::AssetKind::Font) {
            return registry.import_font(*resolved);
        } else if constexpr (K == v2::AssetKind::Video) {
            return registry.import_video(*resolved);
        } else if constexpr (K == v2::AssetKind::Audio) {
            return registry.import_audio(*resolved);
        } else {
            static_assert(K != K, "AssetRef::import: unknown AssetKind");
        }
    }

    // ── Auto-registration in manifest ──────────────────────────────────

    /// Register this asset reference in `manifest`.
    /// After this call, `manifest.entry_for(owner())` returns this asset.
    /// First-inserted-wins semantics (duplicate owners are silently
    /// dropped by v2::AssetManifest::add()).
    void register_in(v2::AssetManifest& manifest) const {
        manifest.add(v2::AssetRef{
            .kind     = K,
            .path     = path_,
            .owner    = owner_,
            .required = required_
        });
    }

    // ── Legacy bridge ─────────────────────────────────────────────────

    /// Convert to the non-templated v2::AssetRef used by AssetManifest
    /// and AssetPreflightResolver.  Useful when operating at the boundary
    /// between typed (compile-time) and untyped (runtime) asset handling.
    [[nodiscard]] v2::AssetRef to_v2_ref() const {
        return v2::AssetRef{
            .kind     = K,
            .path     = path_,
            .owner    = owner_,
            .required = required_
        };
    }

private:
    std::string path_;
    std::string owner_;
    bool        required_{true};
};

// ── Convenience type aliases ─────────────────────────────────────────────

using ImageRef = AssetRef<v2::AssetKind::Image>;
using FontRef  = AssetRef<v2::AssetKind::Font>;
using VideoRef = AssetRef<v2::AssetKind::Video>;
using AudioRef = AssetRef<v2::AssetKind::Audio>;

} // namespace chronon3d::assets


// ═══════════════════════════════════════════════════════════════════════════
// Free function factories — chronon3d::asset (lowercase, user-facing)
//
// These are the canonical entry points for creating typed asset references.
// Each accepts an optional v2::AssetManifest* for auto-registration.
//
// Usage:
//     using namespace chronon3d::asset;  // or asset::image(...)
//     auto bg  = image("bg.png", "hero/bg");
//     auto fnt = font("fonts/Inter.ttf", "title", true, &manifest);
// ═══════════════════════════════════════════════════════════════════════════

namespace chronon3d::asset {

/// Create a typed image asset reference.
/// Optionally auto-register into `manifest` if non-null.
inline assets::ImageRef image(std::string           path,
                              std::string           owner    = "",
                              bool                  required = true,
                              v2::AssetManifest*    manifest = nullptr) {
    assets::ImageRef ref(std::move(path), std::move(owner), required);
    if (manifest) ref.register_in(*manifest);
    return ref;
}

/// Create a typed font asset reference.
/// Optionally auto-register into `manifest` if non-null.
inline assets::FontRef font(std::string           path,
                            std::string           owner    = "",
                            bool                  required = true,
                            v2::AssetManifest*    manifest = nullptr) {
    assets::FontRef ref(std::move(path), std::move(owner), required);
    if (manifest) ref.register_in(*manifest);
    return ref;
}

/// Create a typed video asset reference.
/// Optionally auto-register into `manifest` if non-null.
inline assets::VideoRef video(std::string           path,
                              std::string           owner    = "",
                              bool                  required = true,
                              v2::AssetManifest*    manifest = nullptr) {
    assets::VideoRef ref(std::move(path), std::move(owner), required);
    if (manifest) ref.register_in(*manifest);
    return ref;
}

/// Create a typed audio asset reference.
/// Optionally auto-register into `manifest` if non-null.
inline assets::AudioRef audio(std::string           path,
                              std::string           owner    = "",
                              bool                  required = true,
                              v2::AssetManifest*    manifest = nullptr) {
    assets::AudioRef ref(std::move(path), std::move(owner), required);
    if (manifest) ref.register_in(*manifest);
    return ref;
}

} // namespace chronon3d::asset
