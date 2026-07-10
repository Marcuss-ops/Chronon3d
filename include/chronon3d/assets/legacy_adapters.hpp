// =============================================================================
// chronon3d/assets/legacy_adapters.hpp
//
// M1.7 Step 2 (post-Phase-A1 close-out, 2026-07-10) — Legacy Adapters per
// la safe migration Asset Readiness V2.
//
// 2 funzioni free additive rimaste (`make_asset_ref` + `register_path`),
// puramente header-only inline, per convertire i pattern asset legacy
// (path raw sparsi + asset discovery render-time + fallback silenziosi)
// ai nuovi tipi V2 canonici POD (`AssetRef` + `AssetManifest`) definiti
// in `asset_readiness_v2.hpp`. Non modificano lo stato, non alloccano
// singleton/registry/cache/service-locator.
//
// L'adapter 3 (`accumulate_preflight_result`) è stato rimosso il 2026-07-10
// (cleanup Phase A1) perché dipendeva dall'`AssetPreflightResult` stub
// SEMPRE-VERDE rimosso da `asset_readiness_v2.hpp`. Un
// `accumulate_preflight_result` su un result SEMPRE-VERDE generava FALSI
// VERDI lato consumer — incompatibile con la regola "preflight che dice
// sempre OK è peggio dell'assenza di preflight".
//
// File destinato all'eliminazione completa a Step 4 (TICKET-ASSET-READINESS)
// — i 2 adapter rimasti diventano obsoleti quando il render graph + il
// content migrano completamente alla nuova asset readiness (`Step 3`).
//
// Vincoli AGENTS.md v0.1 (Cat-3 freeze):
//   * ZERO nuovi singleton / registry / cache / service-locator.
//   * NO #include <msdfgen> / <libtess2> / <unicode[/...]>.
//   * ZERO modifiche al codice esistente (tutti i test preesistenti
//     rimangono PASS bit-identical — gli adapter sono AGGIUNTI ma NON
//     chiamati dal render graph / scene builder / composition).
//   * FNV-1a cache key invariato (gli adapter NON toccano il cache path).
//   * ABI pubblico espanso con 2 (post-A1) nuove free functions (non 2
//     nuove classi) → ABI footprint incrementale trascurabile.
//
// Namespace: `chronon3d::assets::v2` (stesso di `asset_readiness_v2.hpp`).
// =============================================================================

#pragma once

#include <chronon3d/assets/asset_readiness_v2.hpp>

#include <string>

namespace chronon3d::assets::v2 {

// -----------------------------------------------------------------------------
// Adapter 1a — `make_asset_ref` — POD builder stateless.
//
// Dato un `AssetKind` + un path raw (relativo a `assets_root` o assoluto)
// + un owner canonical (es. `"CenterTextOptions/label"`,
// `"SceneBuilder::image(\"hero\")"`, `"TimelineBuilder::camera"`), produce
// un `AssetRef` con i field corretti. NON aggiunge al manifest — usa
// `register_path()` (sotto) per il convenience wrapper `make + add`.
//
// `required` default = `true` (asset hard-required). Step 3 popolerà il
// flag `required = false` per i soft-optional asset (es. fallback font).
//
// NON noexcept: la conversione `std::string` path → `AssetRef::path` può
// allocare su OOM. Propaga l'eccezione honestamente (NO silent failure —
// AGENTS v0.1 §"Renderer non inventa fallback").
// -----------------------------------------------------------------------------
[[nodiscard]] inline AssetRef
make_asset_ref(AssetKind kind,
               const std::string& path,
               const std::string& owner,
               bool required = true) {
    return AssetRef{
        .kind     = kind,
        .path     = path,
        .owner    = owner,
        .required = required
    };
}

// -----------------------------------------------------------------------------
// Adapter 1b — `register_path` — convenience wrapper `make_asset_ref + add`.
//
// Crea un `AssetRef` e lo aggiunge al `AssetManifest` in un colpo solo.
// Match esatto con la spec del ticket TICKET-ASSET-READINESS.md §Step 2:
// "wrapper che crea un `AssetRef{kind = Font, path = path, owner = "..."}`
// e lo aggiunge al `AssetManifest` della scena a startup".
//
// NON noexcept: `AssetManifest::add(AssetRef)` può allocare (vector
// push_back + unordered_map emplace) su OOM. Propaga l'eccezione
// honestamente.
//
// First-inserted-wins su `owner` duplicato (canonical semantics di
// `AssetManifest::add` da Step 1 — `std::unordered_map::emplace` su
// chiave esistente è no-op dal C++11, preserva l'originale).
// -----------------------------------------------------------------------------
inline void
register_path(AssetManifest& manifest,
              AssetKind kind,
              const std::string& path,
              const std::string& owner,
              bool required = true) {
    manifest.add(make_asset_ref(kind, path, owner, required));
}

// -----------------------------------------------------------------------------
// Adapter `accumulate_preflight_result` — REMOVED 2026-07-10 (Phase A1
// cleanup).
//
// Operava sull'`AssetPreflightResult` stub SEMPRE-VERDE rimosso da
// `asset_readiness_v2.hpp`: settava `result.ok = false` se `!found` e
// appendeva `ref` a `result.missing`. Rimosso perché dipendere da un
// type la cui rimozione è già decisa + perché l'accumulo partiva da
// un result SEMPRE-VERDE (falsi verdi sul lato consumer). Il preflight
// canonico reale (con accumulator semantico built-in) vive in
// `include/chronon3d/assets/asset_preflight_resolver.hpp` — namespace
// `chronon3d::` (NON `chronon3d::assets::v2`).
//
// Se mai servirà di nuovo, reintrodurre SOLO dopo che
// `chronon3d::AssetPreflightResolver` canonico esponga un'API typed
// result-aware concreta (forward-point a TICKET-ASSET-READINESS Step 3
// reale + ADR-016 §Decision 2). Ad ogni modo non reintrodurre MAI
// `chronon3d::assets::v2::AssetPreflightResult` (compile-fail gate #18).
// -----------------------------------------------------------------------------

} // namespace chronon3d::assets::v2
