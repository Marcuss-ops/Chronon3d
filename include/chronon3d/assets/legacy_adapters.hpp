// =============================================================================
// chronon3d/assets/legacy_adapters.hpp
//
// Phase A2 close-out (2026-07-10) — namespace flattened, POD renamed.
//
// 2 funzioni free additive rimaste (`make_asset_ref` + `register_path`),
// puramente header-only inline, per convertire i pattern asset legacy
// (path raw sparsi + asset discovery render-time + fallback silenziosi)
// ai tipi POD canonici (`InternalAssetRef` + `AssetManifest`) definiti
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
// File destinato all'eliminazione completa a A2-finalize (commit finale
// di Phase A2) — i 2 adapter rimasti diventano obsoleti quando tutto il
// content code migra alla nuova asset readiness (v2 sinonimo di legacy).
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
// Namespace: `chronon3d::assets` (FLAT, ex-`chronon3d::assets::v2`).
// =============================================================================

#pragma once

#include <chronon3d/assets/asset_readiness_v2.hpp>

#include <string>

namespace chronon3d::assets {

// -----------------------------------------------------------------------------
// Adapter 1a — `make_asset_ref` — POD builder stateless.
//
// Dato un `AssetKind` + un path raw (relativo a `assets_root` o assoluto)
// + un owner canonical (es. `"CenterTextOptions/label"`,
// `"SceneBuilder::image(\"hero\")"`, `"TimelineBuilder::camera"`), produce
// un `InternalAssetRef` con i field corretti. NON aggiunge al manifest — usa
// `register_path()` (sotto) per il convenience wrapper `make + add`.
//
// `required` default = `true` (asset hard-required).
//
// NON noexcept: la conversione `std::string` path → `InternalAssetRef::path`
// può allocare su OOM. Propaga l'eccezione honestamente (NO silent failure —
// AGENTS v0.1 §"Renderer non inventa fallback").
// -----------------------------------------------------------------------------
[[nodiscard]] inline InternalAssetRef
make_asset_ref(AssetKind kind,
               const std::string& path,
               const std::string& owner,
               bool required = true) {
    return InternalAssetRef{
        .kind     = kind,
        .path     = path,
        .owner    = owner,
        .required = required
    };
}

// -----------------------------------------------------------------------------
// Adapter 1b — `register_path` — convenience wrapper `make_asset_ref + add`.
//
// Crea un `InternalAssetRef` e lo aggiunge al `AssetManifest` in un colpo solo.
// Match esatto con la spec del ticket TICKET-ASSET-READINESS.md §Step 2:
// "wrapper che crea un `InternalAssetRef{kind = Font, path = path, owner = "..."}`
// e lo aggiunge al `AssetManifest` della scena a startup".
//
// NON noexcept: `AssetManifest::add(InternalAssetRef)` può allocare (vector
// push_back + unordered_map emplace) su OOM. Propaga l'eccezione
// honestamente.
//
// First-inserted-wins su `owner` duplicato (canonical semantics di
// `AssetManifest::add` — `std::unordered_map::emplace` su
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
// `chronon3d::` (NON `chronon3d::assets`).
//
// Se mai servirà di nuovo, reintrodurre SOLO dopo che
// `chronon3d::AssetPreflightResolver` canonico esponga un'API typed
// result-aware concreta (forward-point a TICKET-ASSET-READINESS Step 3
// reale + ADR-016 §Decision 2). Ad ogni modo non reintrodurre MAI
// `chronon3d::assets::v2::AssetPreflightResult` (compile-fail gate #18).
//
// Se mai servirà di nuovo `accumulate_preflight_result`, NON reintrodurlo
// come caller dell'`AssetPreflightResult` STUB: deve operare su
// `chronon3d::AssetPreflightResult` canonico.
// -----------------------------------------------------------------------------

} // namespace chronon3d::assets
