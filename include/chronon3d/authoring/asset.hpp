// ═══════════════════════════════════════════════════════════════════════════
// authoring/asset.hpp — thin authoring-side helper for asset paths.
//
// Audit §10 (process-wide asset root ripout) calls for a thin authoring
// syntax that authors an asset by logical path WITHOUT introducing a
// per-composition resolver or singleton.  Resolution already lives in
// the canonical `chronon3d::assets::AssetResolver` (per-runtime, owned
// by `RenderRuntime::resolver()` since WP-8 PR 8.0) and consumes the
// typed `chronon3d::assets::AssetRef<K>` (compile-time asset-kind
// marker carrying path + owner + required).
//
// This header adds ONE pure-value factory function (`asset(path)` family)
// that returns the canonical `assets::AssetRef<K>`.  It does NOT
// introduce a new resolver, registry, singleton, or factory.
// Cat-3 compliant per AGENTS.md (no new SDK singleton/registry).
//
// Usage:
//   using namespace chronon3d::authoring;
//   layer.image("logo", asset("images/logo.png"));         // AssetKind::Image (default)
//   layer.text("HELLO").font(
//       asset<assets::AssetKind::Font>("fonts/Inter-Bold.ttf"), 100);
//
// Resolution semantics:
//   `asset("path")` ONLY constructs the lightweight typed marker —
//   resolution happens at materialization time against the per-runtime
//   resolver (`software_renderer.runtime().resolver()` for SDK
//   consumers; the literal engine handle for process wiring).  Callers
//   MUST prime the resolver via `engine.set_assets_root()` OR construct
//   the SoftwareRenderer with a `Config` whose `assets_root` field is
//   populated; otherwise the path is preserved verbatim and surfaces
//   as a preflight FAIL (the desired hard-fail per audit §10).
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/assets/asset_ref.hpp>      // chronon3d::assets::AssetRef<K>

#include <string>
#include <utility>

namespace chronon3d::authoring {

/// `asset("path")` — typed authoring-side asset path marker.
///
/// Returns `chronon3d::assets::AssetRef<K>` — the canonical typed asset
/// reference carrying compile-time AssetKind and a (path, owner,
/// required) triplet.  Pure value type — no resolution happens here.
///
/// Default asset kind is `AssetKind::Image` (the most common authoring
/// case).  For fonts overwrite with `asset<AssetKind::Font>("...")`.
///
/// `required = true` (default) means the path is a hard dependency for
/// preflight validation.  Callers that need soft dependencies can pass
/// `false` explicitly at the cost of bypassing the canonical typed
/// marker (the audit's thin API only guarantees hard path semantics).
template <assets::AssetKind K = assets::AssetKind::Image>
[[nodiscard]] inline assets::AssetRef<K>
asset(std::string path, std::string owner = "") {
    return assets::AssetRef<K>(std::move(path), std::move(owner),
                                /*required=*/true);
}

} // namespace chronon3d::authoring
