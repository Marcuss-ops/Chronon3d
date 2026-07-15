// ═══════════════════════════════════════════════════════════════════════════
// authoring/asset.hpp — thin authoring-side helper for logical asset paths.
//
// `asset("path")` is deliberately resolution-free.  It carries only the
// logical path metadata and converts to the canonical typed AssetRef requested
// by the receiving authoring API:
//
//   layer.image("logo", asset("images/logo.png"));
//   layer.text("HELLO").font(asset("fonts/Inter.ttf"), 100);
//
// Resolution remains owned by the per-runtime assets::AssetResolver.  This
// header introduces no resolver, registry, singleton, CWD fallback, or global
// asset root.
//
// The explicit `asset<AssetKind::K>(...)` form remains available for callers
// that need to store a concretely typed AssetRef before passing it onward.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/assets/asset_ref.hpp>

#include <string>
#include <utility>

namespace chronon3d::authoring {

/// Pure-value logical asset path used by the ergonomic `asset("...")` form.
///
/// The receiving API supplies the asset kind through its parameter type:
/// `Layer::image(..., ImageRef)` requests ImageRef, while
/// `Text::font(FontRef, ...)` requests FontRef.  Conversion only wraps the
/// logical path in the existing AssetRef type; it never resolves filesystem
/// state.
class AssetPath final {
public:
    // Backward-compatible default-kind marker for existing code that inspects
    // `decltype(asset("..."))::kind`.  Contextual conversions are not limited
    // to Image and remain the preferred ergonomic path.
    static constexpr assets::AssetKind kind = assets::AssetKind::Image;

    explicit AssetPath(std::string path, std::string owner = {})
        : path_(std::move(path)), owner_(std::move(owner)) {}

    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] const std::string& owner() const noexcept { return owner_; }
    [[nodiscard]] constexpr bool required() const noexcept { return true; }

    template <assets::AssetKind K>
    [[nodiscard]] operator assets::AssetRef<K>() const {
        return assets::AssetRef<K>(path_, owner_, /*required=*/true);
    }

private:
    std::string path_;
    std::string owner_;
};

/// Context-typed authoring marker.  The destination overload determines K.
[[nodiscard]] inline AssetPath asset(std::string path, std::string owner = {}) {
    return AssetPath(std::move(path), std::move(owner));
}

/// Explicit typed form retained for stored references and compatibility.
template <assets::AssetKind K>
[[nodiscard]] inline assets::AssetRef<K>
asset(std::string path, std::string owner = {}) {
    return assets::AssetRef<K>(std::move(path), std::move(owner),
                               /*required=*/true);
}

} // namespace chronon3d::authoring
