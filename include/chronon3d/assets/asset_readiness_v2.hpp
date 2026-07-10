// =============================================================================
// chronon3d/assets/asset_readiness_v2.hpp
//
// Phase A2 close-out #2/3 (2026-07-10) — Asset Readiness POD type-erased
// storage. The `AssetManifest` class body has been PROMOTED out of this
// header to <chronon3d/assets/asset_manifest.hpp>, where it lives as the
// top-level canonical `chronon3d::AssetManifest` (with a `using` alias
// `chronon3d::assets::AssetManifest` at the bottom).
//
// 2 simboli pubblici canonici POD:
//
//   * `AssetKind`        : enum Font/Image/Video/Audio (POD, 4 valori).
//   * `InternalAssetRef` : POD `{kind, path, owner, required}`. Type-erased
//                          internal storage slot (Phase A2 rename from
//                          the previous `v2::AssetRef`).
//
// 1 simbolo NON più in questo header:
//
//   * `AssetManifest`    : canonical class body moved (Phase A2 #2/3) to
//                          <chronon3d/assets/asset_manifest.hpp>. Include
//                          THAT file for `AssetManifest` (or use the flat-
//                          namespace alias `chronon3d::assets::AssetManifest`
//                          once that header is included). This file does
//                          NOT define `assets::AssetManifest` any more —
//                          pre-A2 callers that did `using
//                          chronon3d::assets::AssetManifest;` from this
//                          header see a compile error and migrate to
//                          <chronon3d/assets/asset_manifest.hpp>.
//
// Filename `asset_readiness_v2.hpp` is preserved for ABI compatibility
// (downstream consumers might include via this exact path); the
// canonical Phase-A2 change is the namespace-flattening (A2 #1) AND
// the AssetManifest-promotion (A2 #2). The file is destined for
// deletion in A2 #3 (#3 micro-commit).
//
// Vincoli AGENTS.md v0.1 (Cat-3 freeze):
//   * ZERO nuovi singleton / registry / cache / service-locator.
//   * NO #include <msdfgen> / <libtess2> / <unicode[/...]>.
//   * ABI pubblico: i 2 POD canonici + `chronon3d::AssetRegistry` +
//     `chronon3d::AssetType` esistenti restano intatti e distinti.
//   * Compile-fail gate #18 in `tools/check_architecture_boundaries.sh`
//     vieta la re-introduzione di `assets::v2::AssetPreflightResult` /
//     `assets::v2::AssetPreflightResolver` / `accumulate_preflight_result`.
//   * Compile-fail gate #21 in `tools/check_architecture_boundaries.sh`
//     vieta la re-introduzione di `assets::v2::AssetKind` / `::AssetRef` /
//     `::AssetManifest` + `to_v2_ref()`.
//
// Namespace: `chronon3d::assets` (FLAT, ex-`chronon3d::assets::v2`).
// Il `AssetKind` (4 valori: Font/Image/Video/Audio) ha semantica
// manifest/owner-centric distinta da `chronon3d::AssetType` (5 valori:
// Image/Font/Video/Audio/Mesh/Unknown, in `asset_metadata.hpp`).
// Coesistono come discriminators distinti (metadata vs manifest owner-key),
// NON si sostituiscono.
// =============================================================================

#pragma once

#include <string>

namespace chronon3d::assets {

// -----------------------------------------------------------------------------
// `AssetKind` — discriminator minimo per il `InternalAssetRef` del manifest.
//
// 4 valori, sizeof 1 byte. Distinto da `chronon3d::AssetType` (5 valori
// inclusi Mesh + Unknown, semantica metadata/registry).
// -----------------------------------------------------------------------------
enum class AssetKind : unsigned char {
    Font  = 0,
    Image = 1,
    Video = 2,
    Audio = 3
};

// -----------------------------------------------------------------------------
// `InternalAssetRef` — reference canonico a un asset richiesto da una scena.
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
// -----------------------------------------------------------------------------
struct InternalAssetRef {
    AssetKind  kind{AssetKind::Image};
    std::string path{};
    std::string owner{};
    bool        required{true};
};

// -----------------------------------------------------------------------------
// (REMOVED 2026-07-10 — Phase A2 #2/3)
//
// `AssetManifest` (canonical implementation) lived here from Phase A2 #1/3
// (after the v2 namespace flatten) until Phase A2 #2/3. It has been moved
// to `<chronon3d/assets/asset_manifest.hpp>` where it is the top-level
// canonical `chronon3d::AssetManifest`. The flat-namespace alias
// `chronon3d::assets::AssetManifest = chronon3d::AssetManifest` is at
// the bottom of THAT header. Do NOT re-introduce the class body here
// (ODR violation risk + violates the user's "solo `AssetManifest`" rule).
//
// Earlier (M1.7 / pre-A2):
//   * Phase A1 (2026-07-10) removed the always-green stubs
//     `AssetPreflightResult` + `AssetPreflightResolver` from this header
//     (compile-fail gate #18 forbids re-introduction).
//   * A2 #1 (2026-07-10) flattened `chronon3d::assets::v2` to `chronon3d::assets`.
//   * A2 #2 (2026-07-10) promoted `AssetManifest` out of this header.
// -----------------------------------------------------------------------------

} // namespace chronon3d::assets
