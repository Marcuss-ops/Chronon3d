// =============================================================================
// chronon3d/assets/asset_readiness_v2.hpp
//
// Phase A2 close-out (2026-07-10) — Asset Readiness POD types moved to the
// flat namespace `chronon3d::assets` (no more `v2::` sub-namespace).
//
// 3 simboli pubblici canonici POD per la dichiarazione canonica degli asset
// richiesti da una scena:
//
//   * `AssetKind`        : enum Font/Image/Video/Audio (POD, 4 valori).
//   * `InternalAssetRef` : POD `{kind, path, owner, required}`. Type-erased
//                          internal storage slot (Phase A2 rename from the
//                          previous `v2::AssetRef`). Used as the entry type
//                          of `AssetManifest` and as the storage cell of the
//                          templated public-facing `AssetRef<K>` wrapper
//                          (`asset_ref.hpp`).
//   * `AssetManifest`    : value type con `add(entry)` + `entry_for(owner)`
//                          + `all()` + `size()` + `empty()`. First-inserted-
//                          wins on owner duplicates.
//
// Filename `asset_readiness_v2.hpp` is preserved for ABI compatibility (some
// downstream consumers include via this path); the namespace-and-POD-rename
// inside is the canonical Phase-A2 change. The file is destined for
// deletion in A2-finalize (compile-fail gate #21 forbids reintroduction of
// any `v2::*` symbol or `to_v2_ref()` once the canonical flat-namespace
// types are wired into `AssetManifest`).
//
// Lo `AssetPreflightResult` e `AssetPreflightResolver` stub SEMPRE-VERDI
// (ritorno `{ok=true, missing=[]}` fisso) che originariamente occupavano
// questo header sono stati rimossi in cleanup Phase A1 (2026-07-10, TICKET
// di cleanup). Un preflight che dice sempre "OK" è peggio dell'assenza di
// preflight perché genera falsi verdi. Il preflight canonico reale vive
// in `include/chronon3d/assets/asset_preflight_resolver.hpp` — namespace
// `chronon3d::` (NON `chronon3d::assets`) e produce
// `chronon3d::AssetPreflightResult` con `issues[]` strutturati.
//
// Vincoli AGENTS.md v0.1 (Cat-3 freeze):
//   * ZERO nuovi singleton / registry / cache / service-locator.
//   * NO #include <msdfgen> / <libtess2> / <unicode[/...]>.
//   * ABI pubblico preservato: i 3 POD canonici + `chronon3d::AssetRegistry` +
//     `chronon3d::AssetType` esistenti restano intatti e distinti.
//   * Compile-fail gate #18 in `tools/check_architecture_boundaries.sh`
//     vieta la re-introduzione di `assets::v2::AssetPreflightResult` /
//     `assets::v2::AssetPreflightResolver` / `accumulate_preflight_result`.
//   * Compile-fail gate #21 in `tools/check_architecture_boundaries.sh`
//     vieta la re-introduzione di `assets::v2::AssetKind` / `::AssetRef` /
//     `::AssetManifest` + `to_v2_ref()` (post-Phase-A2 de-namespacing).
//
// Namespace: il nuovo `chronon3d::assets` (FLAT, ex-`chronon3d::assets::v2`)
// è distinto da `chronon3d::AssetType` (in `asset_metadata.hpp`) che ha
// semantica metadata/registry (5+ tipi inclusi Mesh + Unknown). Il
// `AssetKind` ha semantica manifest/owner-centric (4 tipi:
// Font/Image/Video/Audio) — i due tipi coesistono come concetti distinti
// (metadata discriminator vs manifest owner-key), NON si sostituiscono.
// =============================================================================

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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
// Phase A2 rename: era `v2::AssetRef` (POD). La ridenominazione a
// `InternalAssetRef` esplicita il ruolo di type-erased POD che il
// templated wrapper pubblico `AssetRef<K>` (`asset_ref.hpp`) usa per
// storage + delegazione. Il POD è POD-friendly: copy/const-evaluate-friendly,
// NON singleton, NON service-locator.
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
// `AssetManifest` — raccolta canonica di `InternalAssetRef` per una scena.
//
// POD-friendly value type, copy/const-evaluate-friendly, NON singleton.
// Internamente tiene:
//   * `m_entries`  : vector preserva insertion order (per `all()`).
//   * `m_by_owner` : unordered_map per O(1) `entry_for(owner)` lookup.
//
// `Entry` è un type alias per `InternalAssetRef` (un manifest entry È un
// `InternalAssetRef` + i metadata dell'inserimento). Match esatto con la
// spec del ticket (`std::optional<AssetManifest::Entry> entry_for(owner)`).
// -----------------------------------------------------------------------------
class AssetManifest {
public:
    using Entry = InternalAssetRef;  // type alias per spec ticket

    /// Aggiunge un asset reference al manifest. `ref.owner` deve essere
    /// unico per scena; in caso di duplicato, `entry_for(owner)` ritornerà
    /// il PRIMO inserito (first-inserted-wins: `std::unordered_map::emplace`
    /// su chiave esistente è no-op dal C++11, preserva l'originale) e
    /// `all()` ritorna TUTTI gli inserimenti (in insertion order, con i
    /// duplicati visibili).
    void add(InternalAssetRef ref) {
        m_by_owner.emplace(ref.owner, ref);
        m_entries.push_back(std::move(ref));
    }

    /// Lookup per owner. Ritorna `std::nullopt` se l'owner non è presente.
    /// O(1) via `m_by_owner`.
    [[nodiscard]] std::optional<Entry>
    entry_for(const std::string& owner) const noexcept {
        const auto it = m_by_owner.find(owner);
        if (it == m_by_owner.end()) return std::nullopt;
        return it->second;
    }

    /// Ritorna tutti gli `InternalAssetRef` in insertion order. O(N).
    /// NB: ritorna by-value `std::vector<InternalAssetRef>` per semplicità
    /// (no reference-stability contract). I consumer (preflight caller) lo
    /// consumano in un singolo range-for.
    [[nodiscard]] std::vector<InternalAssetRef> all() const {
        return m_entries;
    }

    /// Helper ergonomico: numero totale di entries nel manifest.
    [[nodiscard]] std::size_t size() const noexcept {
        return m_entries.size();
    }

    /// Helper ergonomico: `true` se il manifest non contiene entries.
    [[nodiscard]] bool empty() const noexcept {
        return m_entries.empty();
    }

private:
    std::vector<InternalAssetRef> m_entries{};
    std::unordered_map<std::string, InternalAssetRef> m_by_owner{};
};

// -----------------------------------------------------------------------------
// (REMOVED 2026-07-10 — Phase A1 cleanup)
//
// `AssetPreflightResult` + `AssetPreflightResolver` vennero originariamente
// aggiunti come stubs SEMPRE-VERDI (M1.7 Step 1) con
// `preflight(manifest) -> {ok=true, missing=[]}` hardcoded e i 4 metodi
// privati `check_font / check_image / check_video / check_audio` che
// ritornavano tutti `true` in attesa della logica reale da popolare a
// Step 3. Sono stati rimossi perché:
//
//   1. Il preflight canonico reale (`AssetPreflightResolver::check(...)` /
//      `::check_manifest(...)` con `issues[]` strutturati) VIVE GIÀ in
//      `include/chronon3d/assets/asset_preflight_resolver.hpp` namespace
//      `chronon3d::` (NON `chronon3d::assets`) ed è già usato dal CLI
//      (`command_preflight`, `command_still`, video exporters) + dai test
//      canonici (`tests/visual/timeline/test_asset_readiness.cpp`).
//
//   2. Uno stub che ritorna sempre {ok=true, missing=[]} è peggio
//      dell'assenza di preflight perché genera FALSI VERDI: asset
//      mancanti passano silenziosamente e il runtime continua a renderare
//      usando fallback impliciti (draw_black_rect, default_font, …),
//      violando la regola AGENTS §"Renderer non inventa fallback".
//
// `legacy_adapters.hpp` ⇒ `accumulate_preflight_result(...)` rimosso per
// la stessa ragione (operava sull'`AssetPreflightResult` stub).
//
// Wrapper canonico per "gli asset sono pronti?" post-A1:
//   `chronon3d::AssetPreflightResolver::check(scene, resolver, mode)` /
//   `::check_manifest(manifest, resolver)` → `chronon3d::AssetPreflightResult`
//   con `ok()` + `issues[]`. Vive in `asset_preflight_resolver.hpp`.
//
// Compile-fail gate #18 in `tools/check_architecture_boundaries.sh` impedisce
// la re-introduzione di: `assets::v2::AssetPreflightResult` /
// `assets::v2::AssetPreflightResolver` / `accumulate_preflight_result`.
// -----------------------------------------------------------------------------

} // namespace chronon3d::assets
