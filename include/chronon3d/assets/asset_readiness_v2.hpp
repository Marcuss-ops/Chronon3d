// =============================================================================
// chronon3d/assets/asset_readiness_v2.hpp
//
// M1.7 Step 1 (post-Phase-A1 close-out, 2026-07-10) — Asset Readiness V2 POD
// types: 3 simboli pubblici canonici.
//
// 3 simboli pubblici canonici POD per la dichiarazione canonica degli asset
// richiesti da una scena, in namespace `chronon3d::assets::v2`:
//   * `AssetKind`    : enum Font/Image/Video/Audio (POD).
//   * `AssetRef`     : POD { kind, path, owner, required }.
//   * `AssetManifest`: value type con `add(entry)` + `entry_for(owner)` + `all()`.
// Sostituiscono i path raw sparsi nel content code e abilitano il typed
// `chronon3d::assets::AssetRef<K>` wrapper (`asset_ref.hpp`) con factory
// `asset::image/font/video/audio(...)`.
//
// Lo `AssetPreflightResult` e `AssetPreflightResolver` stub SEMPRE-VERDI
// (ritorno `{ok=true, missing=[]}` fisso) che originariamente occupavano
// questo header sono stati rimossi in cleanup Phase A1 (2026-07-10, TICKET
// di cleanup). Un preflight che dice sempre "OK" è peggio dell'assenza di
// preflight perché genera falsi verdi. Il preflight canonico reale vive
// in `include/chronon3d/assets/asset_preflight_resolver.hpp` — namespace
// `chronon3d::` (NON `chronon3d::assets::v2`) e produce
// `cronon::AssetPreflightResult` con `issues[]` strutturati.
//
// Vincoli AGENTS.md v0.1 (Cat-3 freeze):
//   * ZERO nuovi singleton / registry / cache / service-locator.
//   * NO #include <msdfgen> / <libtess2> / <unicode[/...]>.
//   * ZERO modifiche al codice esistente (i test preesistenti rimangono
//     PASS bit-identical — i consumer reali `asset_ref.hpp` usano SOLO i 3
//     POD rimasti, non i tipi di preflight rimossi).
//   * ABI pubblico preservato: 3 POD canonici + `chronon3d::AssetRegistry` +
//     `chronon3d::AssetType` esistenti restano intatti e distinti.
//   * Compile-fail gate #18 in `tools/check_architecture_boundaries.sh`
//     vieta la re-introduzione di `assets::v2::AssetPreflightResult` /
//     `assets::v2::AssetPreflightResolver` / `accumulate_preflight_result`.
//
// Namespace: `chronon3d::assets::v2` distinto da `chronon3d::AssetType`
// (in `asset_metadata.hpp`) che ha semantica metadata/registry (5+ tipi
// inclusi Mesh + Unknown). Il `AssetKind` ha semantica
// manifest/owner-centric (4 tipi: Font/Image/Video/Audio) — i due tipi
// coesistono come concetti distinti (metadata discriminator vs
// manifest owner-key), NON sostituiscono `AssetType`.
//
// Forward-point: future migration del render loop per usare
// `AssetKind → v2::AssetRef` come keys canoniche del preflight canonico
// è design-FROZEN (`docs/tickets/TICKET-ASSET-READINESS.md` + ADR-016
// §Decision 2). Step 4 eliminerà i fallback silenziosi legacy.
// =============================================================================

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d::assets::v2 {

// -----------------------------------------------------------------------------
// `AssetKind` — discriminator minimo per il `AssetRef` del manifest.
//
// 4 valori, sizeof 1 byte. Distinto da `chronon3d::AssetType` (5 valori
// inclusi Mesh + Unknown, semantica metadata/registry).
//
// Step 3 (forward-point) aggiungerà il mapping `AssetKind → AssetType`
// canonico quando i consumer inizieranno a usare `AssetManifest` per
// il wiring verso `AssetRegistry` esistente.
// -----------------------------------------------------------------------------
enum class AssetKind : unsigned char {
    Font  = 0,
    Image = 1,
    Video = 2,
    Audio = 3
};

// -----------------------------------------------------------------------------
// `AssetRef` — reference canonico a un asset richiesto da una scena.
//
//   * `kind`     : `AssetKind` (Font/Image/Video/Audio).
//   * `path`     : path raw (relativo a `assets_root` o assoluto).
//                  Step 1: il path è solo metadata. Step 2 wireà i
//                  legacy adapters che traducono `font_path / image_path /
//                  video_path / audio_path` sparsi in `AssetRef`. Step 3
//                  popolerà `path` dal `SceneBuilder::image(...) / video(...)
//                  / audio(...)` API; il path raw sparisce dal content code.
//   * `owner`    : identificatore logico del "proprietario" dell'asset
//                  (es. "LightPulse/text/label", "CenterTextOptions/label",
//                  "SceneBuilder::image(\"hero\")"). Discriminant per
//                  `AssetManifest::entry_for(owner)`. Unico per scena.
//   * `required` : se true, l'asset è HARD-REQUIRED (mancanza = FAIL
//                  preflight con `ok=false` + `missing` non-empty);
//                  se false, l'asset è soft-optional (mancanza =
//                  `ok=true` + `missing` non-empty post-Step-3).
//                  Step 1: il flag è solo metadata; il resolver non lo
//                  consuma ancora.
// -----------------------------------------------------------------------------
struct AssetRef {
    AssetKind  kind{AssetKind::Image};
    std::string path{};
    std::string owner{};
    bool        required{true};
};

// -----------------------------------------------------------------------------
// `AssetManifest` — raccolta canonica di `AssetRef` per una scena.
//
// POD-friendly value type, copy/const-evaluate-friendly, NON singleton.
// Internamente tiene:
//   * `m_entries`  : vector preserva insertion order (per `all()`).
//   * `m_by_owner` : unordered_map per O(1) `entry_for(owner)` lookup.
//
// `Entry` è un type alias per `AssetRef` (un manifest entry È un
// `AssetRef` + i metadata dell'inserimento). Match esatto con la spec
// del ticket (`std::optional<AssetManifest::Entry> entry_for(owner)`).
// -----------------------------------------------------------------------------
class AssetManifest {
public:
    using Entry = AssetRef;  // type alias per spec ticket

    /// Aggiunge un asset reference al manifest. `ref.owner` deve essere
    /// unico per scena; in caso di duplicato, `entry_for(owner)` ritornerà
    /// il PRIMO inserito (first-inserted-wins: `std::unordered_map::emplace`
    /// su chiave esistente è no-op dal C++11, preserva l'originale) e
    /// `all()` ritorna TUTTI gli inserimenti (in insertion order, con i
    /// duplicati visibili). Step 3 aggiungerà la `FAIL` esplicita su
    /// owner duplicato.
    void add(AssetRef ref) {
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

    /// Ritorna tutti gli `AssetRef` in insertion order. O(N).
    /// NB: ritorna by-value `std::vector<AssetRef>` per semplicità
    /// (no reference-stability contract). I consumer (post-Step-3
    /// preflight caller) lo consumano in un singolo range-for.
    [[nodiscard]] std::vector<AssetRef> all() const {
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
    std::vector<AssetRef> m_entries{};
    std::unordered_map<std::string, AssetRef> m_by_owner{};
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
//      `chronon3d::` (NON `chronon3d::assets::v2`) ed è già usato dal CLI
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

} // namespace chronon3d::assets::v2
