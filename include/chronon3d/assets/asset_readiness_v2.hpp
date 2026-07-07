// =============================================================================
// chronon3d/assets/asset_readiness_v2.hpp
//
// M1.7 Step 1 — Asset Readiness V2 single source of truth.
//
// 5 nuovi simboli pubblici canonici che sostituiscono la gestione asset
// legacy (path raw sparsi + asset discovery render-time + fallback silenziosi
// + per-feature Preflight duplicato) e che diventano il contratto SSoT per
// la dichiarazione e il preflight degli asset richiesti da una scena.
//
// Vincoli AGENTS.md v0.1 (Cat-3 freeze):
//   * ZERO nuovi singleton / registry / cache / service-locator.
//   * NO #include <msdfgen> / <libtess2> / <unicode[/...]>.
//   * ZERO modifiche al codice esistente (tutti i test preesistenti
//     rimangono PASS bit-identical).
//   * ABI pubblico preservato (`chronon3d::AssetRegistry` + `chronon3d::AssetType`
//     + `chronon3d::AssetMetadata` esistenti restano intatti e distinti).
//
// Namespace: `chronon3d::assets::v2` distinto da `chronon3d::AssetType`
// (in `asset_metadata.hpp`) che ha semantica metadata/registry (5+ tipi
// inclusi Mesh + Unknown). Il nuovo `AssetKind` ha semantica
// manifest/owner-centric (4 tipi: Font/Image/Video/Audio) — i due tipi
// coesistono come concetti distinti (metadata discriminator vs
// manifest owner-key), NON sostituiscono `AssetType`.
//
// Step 1 è solo AGGIUNTA + superficie minima. Step 2 (legacy adapters) +
// Step 3 (migrate new content + real file-existence checks) +
// Step 4 (eliminate legacy + grep-audit backlog = 0) restano forward-point.
// Vedi `docs/tickets/TICKET-ASSET-READINESS.md` per il piano completo.
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
// `AssetPreflightResult` — output canonico del `AssetPreflightResolver`.
//
//   * `ok`      : `true` se TUTTI gli asset richiesti sono disponibili
//                 (post-Step-3: file esiste + leggibile + kind matches
//                 extension). `false` se almeno un asset manca.
//   * `missing` : lista degli `AssetRef` che NON sono disponibili.
//                 Vuota se `ok == true`. NON vuota se `ok == false`
//                 (post-Step-3). Step 1: sempre vuota perché i check
//                 privati sono no-op stubs che ritornano `true`.
// -----------------------------------------------------------------------------
struct AssetPreflightResult {
    bool ok{true};
    std::vector<AssetRef> missing{};
};

// -----------------------------------------------------------------------------
// `AssetPreflightResolver` — single source of truth per "gli asset sono
// pronti?".
//
// Step 1 superficie minima (header-only inline; no cache; no singleton).
// Il resolver è una value type stateless: copy/const-evaluate-friendly;
// può essere conservato in `RenderSession` (post-Step-3) o in
// `RenderGraphContext` senza dover introdurre nessun nuovo global registry.
//
// Regola finale (vincolante per Chiusura M1.7 Asset):
//   `AssetPreflightResolver::preflight(manifest)` decide se tutti gli
//   asset sono pronti. `RenderJob::start()` chiama preflight UNA volta
//   prima del render loop; se `result.ok == false`, FAIL esplicito con
//   messaggio per ogni `missing`. `Renderer` non inventa fallback
//   (no `use_default_font`, no `draw_black_rect`, no `empty_frame`,
//   no `continue` su missing). PNG scuri vietati.
//
// I 4 check privati `check_font / check_image / check_video / check_audio`
// sono no-op stubs a Step 1 (ritornano sempre `true` → result sempre
// `ok=true, missing=[]`). Step 3 popolerà la logica reale (file esiste +
// kind/extension match + permission readable).
// -----------------------------------------------------------------------------
class AssetPreflightResolver {
public:
    /// Risolve il manifest. Step 1 ritorna SEMPRE `AssetPreflightResult{ok=true, missing=[]}`.
    /// Step 3 wireà la reale attivazione dei check `check_font / check_image / ...`
    /// e popolerà `missing` con gli `AssetRef` che falliscono il check.
    ///
    /// `noexcept` perché la versione Step 1 non alloca né chiama funzioni
    /// throwing. Step 3 (con check reali) potrebbe dover rilasciare
    /// `noexcept` se i check lanciano eccezioni filesystem.
    [[nodiscard]] AssetPreflightResult
    preflight(const AssetManifest& manifest) const noexcept {
        AssetPreflightResult result;
        result.ok = true;  // Step 1: sempre OK; Step 3 popolerà da `check_*`.
        result.missing = {};  // Step 1: mai missing; Step 3 popolerà.

        // Step 3 (forward-point) wireà qualcosa tipo:
        //   for (const auto& ref : manifest.all()) {
        //       if (!check_one(ref)) result.missing.push_back(ref);
        //   }
        //   result.ok = result.missing.empty();
        //
        // Per Step 1 il loop è omesso: i check privati sono no-op stubs
        // che ritornano sempre true, e il codice esistente NON chiama
        // `preflight` (gli adapter sono forward-point a Step 2/3).
        (void)manifest;  // sopprimi unused-parameter warning
        return result;
    }

private:
    // Step 1: tutti i check sono no-op `return true;`. Step 3 popolerà
    // la logica reale (file exists + readable + kind/extension match).
    [[nodiscard]] bool check_font (const AssetRef& /*r*/) const noexcept { return true; }
    [[nodiscard]] bool check_image(const AssetRef& /*r*/) const noexcept { return true; }
    [[nodiscard]] bool check_video(const AssetRef& /*r*/) const noexcept { return true; }
    [[nodiscard]] bool check_audio(const AssetRef& /*r*/) const noexcept { return true; }
};

} // namespace chronon3d::assets::v2
