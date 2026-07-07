// =============================================================================
// chronon3d/assets/legacy_adapters.hpp
//
// M1.7 Step 2 — Legacy Adapters per la safe migration Asset Readiness V2.
//
// 3 funzioni free additive, puramente header-only inline, per convertire
// i pattern asset legacy (path raw sparsi + asset discovery render-time +
// fallback silenziosi) ai nuovi tipi V2 canonici (`AssetRef` +
// `AssetManifest` + `AssetPreflightResult`) definiti in
// `asset_readiness_v2.hpp`. Non modificano lo stato, non alloccano
// singleton/registry/cache/service-locator.
//
// File destinato all'eliminazione allo Step 4 (TICKET-ASSET-READINESS) —
// gli adapter diventano obsoleti quando il render graph + il content
// migrano completamente alla nuova asset readiness (`Step 3` migrate
// new content).
//
// Vincoli AGENTS.md v0.1 (Cat-3 freeze):
//   * ZERO nuovi singleton / registry / cache / service-locator.
//   * NO #include <msdfgen> / <libtess2> / <unicode[/...]>.
//   * ZERO modifiche al codice esistente (tutti i test preesistenti
//     rimangono PASS bit-identical — gli adapter sono AGGIUNTI ma NON
//     chiamati dal render graph / scene builder / composition).
//   * FNV-1a cache key invariato (gli adapter NON toccano il cache path).
//   * ABI pubblico espanso con 3 nuove free functions (non 3 nuove classi)
//     → ABI footprint incrementale trascurabile.
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
// Adapter 2 — `accumulate_preflight_result` — bridge accumulator per
// render-time not-found errors.
//
// Dato un `AssetRef` (l'asset che è stato cercato al render-time) + un
// "found" boolean (true = trovato/caricato, false = not-found al
// render-time), accumula il risultato in un `AssetPreflightResult`
// esistente: setta `result.ok = false` se `!found` + appende `ref` a
// `result.missing`.
//
// Pattern d'uso tipico (post-Step-3, quando i wrapper specifici per
// dominio saranno aggiunti):
//
//   AssetPreflightResult result;  // empty, ok = true, missing = []
//   for (const auto& ref : manifest.all()) {
//       bool found = check_one_for_domain(ref, ...);  // domain-specific
//       accumulate_preflight_result(result, ref, found);
//   }
//   if (!result.ok) FAIL_PREFLIGHT(result.missing);
//
// NON noexcept di design: `result.missing.push_back(ref)` può allocare
// su OOM. Se OOM succede, l'eccezione propaga honestamente al caller
// (post-Step-3: `RenderJob::start()`). NO silent failure (AGENTS v0.1
// §"Renderer non inventa fallback" + §"no silent asset fallback").
// -----------------------------------------------------------------------------
inline void
accumulate_preflight_result(AssetPreflightResult& result,
                            const AssetRef& ref,
                            bool found) {
    if (!found) {
        result.ok = false;
        result.missing.push_back(ref);
    }
}

} // namespace chronon3d::assets::v2
