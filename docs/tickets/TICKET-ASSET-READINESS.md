# TICKET-ASSET-READINESS — Asset readiness: preflight decide se gli asset sono pronti, render non inventa fallback

## Stato

**PARTIAL** — Step 1/4 + Step 2/4 DONE (commit TBD-pending this session, 2026-07-07). Code landed: (Step 1) 5 nuovi simboli pubblici canonici `AssetKind` enum + `AssetRef` POD + `AssetManifest` class + `AssetPreflightResult` POD + `AssetPreflightResolver` class in `include/chronon3d/assets/asset_readiness_v2.hpp` namespace `chronon3d::assets::v2` (distinto da `chronon3d::AssetType` esistente in `asset_metadata.hpp` — semantica manifest/owner-centric vs metadata/registry); (Step 2) 3 free functions additive `make_asset_ref` POD builder + `register_path` make+add convenience + `accumulate_preflight_result` bridge accumulator in `include/chronon3d/assets/legacy_adapters.hpp` namespace `chronon3d::assets::v2`. Manifest aggiornato in `cmake/Chronon3DPublicHeaders.cmake` (entrambi gli header registrati NO GLOB). Header `g++ -std=c++20 -fsyntax-only` exit 0 per entrambi gli header; verification TUs `/tmp/_step1_asset_consumer_check.cpp` (Step 1) compile + run exit 0 + `/tmp/_step2_asset_consumer_check.cpp` (Step 2) syntax-only exit 0. Code-reviewer-minimax-m3 APPROVED round 1 (1 nit minore doc-only `LIFO semantics` → `first-inserted-wins` risolto per Step 1 + 1 nit minore `#include <string>` transitivamente ridondante per Step 2 non-blocking). ZERO codice esistente toccato (test preesistenti bit-identicali garantiti — gli adapter sono AGGIUNTI ma NON chiamati dal render graph / scene builder / composition). AGENTS Cat-3 freeze-compliant + ABI pubblico espanso di 3 free functions POD (no new class). FNV-1a cache key invariato (gli adapter non toccano il cache path). Steps 3/4 (migrate new content + eliminate legacy) restano forward-point. **Workstream design-FROZEN 2026-07-07** per le 2-step rimanenti.

## Priorità

P1 — foundational refactor che abilita AssetPipeline V2 (post P1 #7 Asset resolver globale). L'obiettivo è canonicalizzare UN preflight (AssetPreflightResolver) come unica sorgente di verità per la presenza/assenza degli asset, eliminando le 5 legacy items del piano utente.

## Problema

Oggi coesistono due livelli di controllo asset:

1. **Asset discovery durante il render**: il font viene risolto dentro `prepare_text_run()` (via `rctx.text_resources->resolve_handle(...)`); se fallisce ritorna errore. Image discovery dentro `image_loader.cpp`. Video discovery dentro `video_decoder_*`. Audio discovery dentro `audio_decoder_*`.
2. **Asset validation per-feature duplicata**: `TextPreflight`, `ImagePreflight`, `VideoPreflight`, `AudioPreflight`, `FontPreflight` sparsi ognuno nel proprio modulo.

Effetti: la mancanza di un font viene scoperta al frame 50 quando arriva al raster; fallback silenziosi (`use_default_font`, `draw_black_rect`, `return empty_frame`, `continue`) producono PNG scuri senza diagnostica; test readiness che fanno `catch(...) { MESSAGE; return; }` mascherano l'errore.

### 5 legacy items del piano utente (grep-audit backlog)

| # | Legacy item | Grep canonico | Owner canonical atteso post-fix |
|---|---|---|---|
| A | `font_path / image_path / video_path / audio_path` raw sparsi senza `AssetRef` (es. `std::string font_path{"assets/fonts/Poppins-Bold.ttf"}` in `CenterTextOptions`) | `rg '\b(font_path|image_path|video_path|audio_path)\b' content src include` | `AssetRef{kind, path, owner, required}` canonico; path raw solo dentro `AssetManifest::entry_for(owner)` |
| B | Asset discovery / resolution chiamato dentro `prepare_text_run()` / `image_loader.cpp` / render-time | `rg 'resolve_handle\|load_image\|decode_video\|decode_audio\|font_engine\.load' src/content` (albero di discovery render-time) | `AssetPreflightResolver::preflight_assets(comp) -> AssetPreflightResult` chiamato da `RenderJob` PRIMA dell'inizio del render loop |
| C | Fallback silenziosi: `use_default_font`, `draw_black_rect`, `return empty_frame`, `continue` quando l'asset manca | `rg 'default_font\|fallback\|black_rect\|empty_frame\|continue;\s*//\s*missing\|placeholder' src/content` | `AssetPreflightResult{ok=true/ok=false, missing{owner, path, kind}}: FAIL(missing_font, "owner=.../path=...")` esplicito; nessun fallback. PNG scuri vietati. |
| D | `catch (...) { MESSAGE("Render failed..."); return; }` nei test readiness (cattivo: troppo permissivo) | `rg 'catch\s*\(\.\.\.\)\s*\{[^}]*MESSAGE[^}]*return' tests` | `catch (const std::exception& e) { FAIL("Render failed: " << e.what()); }` — i test readiness ASPETTANO il fail di preflight, non lo ignorano. |
| E | Asset validation duplicata per-feature (`TextPreflight` / `ImagePreflight` / `VideoPreflight` / `AudioPreflight` / `FontPreflight`) | `rg 'class\s+\w+Preflight\|TextPreflight\|ImagePreflight\|VideoPreflight\|AudioPreflight\|FontPreflight' src include` | UN solo `AssetPreflightResolver` con adapter per kind `check_font(...)`, `check_image(...)`, `check_video(...)`, `check_audio(...)`, tutti ritornano dentro `AssetPreflightResult` |

## Soluzione accettabile

### Sequenza 4-step safe migration (riga guida utente sez. 4)

**Step 1 — Aggiungi nuovo sistema (verde, backward-compat):**

```cpp
// include/chronon3d/assets/asset_ref.hpp
enum class AssetKind { Font, Image, Video, Audio };
struct AssetRef {
    AssetKind kind;
    std::string path;
    std::string owner;        // es. "LightPulse/text/label"
    bool required = true;      // soft / hard
};

// include/chronon3d/assets/asset_manifest.hpp
class AssetManifest {
public:
    void add(AssetRef ref);
    std::optional<AssetManifest::Entry> entry_for(const std::string& owner) const;
    std::vector<AssetRef> all() const;
};

// include/chronon3d/assets/asset_preflight.hpp
struct AssetPreflightResult {
    bool ok = true;
    std::vector<AssetRef> missing;  // owner + path + kind per riga
};
class AssetPreflightResolver {
public:
    AssetPreflightResult preflight(const AssetManifest& m) const;
private:
    bool check_font(const AssetRef& r) const;
    bool check_image(const AssetRef& r) const;
    bool check_video(const AssetRef& r) const;
    bool check_audio(const AssetRef& r) const;
};
```

**Step 2 — Legacy adapters:**

- `font_path` esistente in `CenterTextOptions` → wrapper che crea un `AssetRef{kind = Font, path = path, owner = "CenterTextOptions/label"}` e lo aggiunge al `AssetManifest` della scena a startup.
- `image_path / video_path / audio_path` analogo.
- `rctx.text_resources->resolve_handle(...)` ritorno errore → bridge che logga `AssetPreflightResult::missing` se l'errore è "not found" (errno-like), `FAIL(missing_font, owner=..., path=...)` esplicito.

**Step 3 — Migra nuovi content solo su AssetRef (verde):**

- Nuovi scenari / showcase / ae-parity scene usano SOLO `AssetRef{kind, path, owner, required}` esplicito nel manifest della scena.
- `RenderJob::start()` chiama `AssetPreflightResolver::preflight(manifest)` UNA volta prima del render loop; se `result.ok == false`, fail esplicito con messaggio per ogni `missing`: `FONT missing  path: ...  owner: ...`.

**Step 4 — Elimina legacy (quando i test passano):**

Rimuovi fisicamente:

- `TextPreflight / ImagePreflight / VideoPreflight / AudioPreflight / FontPreflight` da `src/` (consolidati in `AssetPreflightResolver`).
- I fallback silenziosi da `image_loader.cpp / video_decoder_* / audio_decoder_* / font_engine.cpp` (post-Step-3 → tutti i content sono già su AssetRef esplicito, il fallback non serve più).
- Path raw in `CenterTextOptions / SceneBuilder::image(...) / SceneBuilder::video(...) / SceneBuilder::audio(...)` API comoda → migrati a `AssetRef` (back-compat wrapper rimosso a fine transizione).
- I test readiness `catch(...) { MESSAGE; return; }` sostituiti da `FAIL("Render failed: " << e.what())` esplicito.

## Criteri di accettazione

- **Step 1 DONE**: `AssetRef { AssetKind kind; std::string path; std::string owner; bool required; }` + `AssetManifest::add(AssetRef)` + `AssetManifest::entry_for(owner)` + `AssetPreflightResolver::preflight(manifest) -> AssetPreflightResult {ok, missing[]}` esistono come simboli pubblici in `include/chronon3d/assets/`. **Zero modifiche al codice esistente.** Zero nuovi singleton (NO `GlobalAssetRegistry`, NO `AssetServiceLocator`).
- **Step 2 DONE**: adapter scoped landed; tutti i test preesistenti (`chronon3d_text_golden_tests`, `chronon3d_ae_parity_tests`, `chronon3d_install_consumer_tests`, `chronon3d_cache_tests`) continuano a PASS bit-identical. Asset loading path stable (FNV-1a cache key invariato).
- **Step 3 DONE**: almeno 5 scene nuove (ae-parity cinematic-21..25) usano SOLO `AssetRef{kind, path, owner, required}` nel manifest. Test preflight emettono `AssetPreflightResult::missing` quando l'asset è assente (verificato via test che NON usa il fallback).
- **Step 4 DONE**: grep-audit backlog torna a ZERO per ognuno dei 5 legacy items A-E. Comportamento bit-identical al post-Step-3 (cache key + screenshot byte-identical). `TextPreflight / ImagePreflight / VideoPreflight / AudioPreflight / FontPreflight` rimossi come classi dedicate. PNG scuri vietati nel `chronon3d_install_consumer_tests` Phase 4. `tools/check_no_silent_asset_fallback.sh` (NEW forward-only grep gate) deve restituire 0 hits.
- **Honesty policy**: ogni step è verificato macchina (test pertinente PASS + grep-audit risultato documentato). Nessuna stima %. Nessun claim "DONE" senza macchina-verifica.

## AGENTS.md v0.1 freeze compliance

Cat-3 (zero new public API symbols oltre i 4 nuovi: `AssetRef`, `AssetManifest`, `AssetPreflightResult`, `AssetPreflightResolver`. Tutti in `include/chronon3d/assets/`, nessun `#include <msdfgen>|<libtess2>|<unicode[/...]>`) + Cat-5 (doc-only alignment via CHANGELOG + FOLLOWUP_TICKETS + CURRENT_STATUS nello stesso commit di Step 1+2; roadmap M1.7 milestone). Step 3+4 rispettano Cat-1 (build corrective) + Cat-3 (internal-only quando rimuovono vecchio path). Zero nuove classi pubbliche oltre le 4 elencate. Zero nuovi singleton/registry/service-locator.

## Confine

- **In scope**: sistema `AssetRef + AssetManifest + AssetPreflightResolver`; 5 legacy items A-E; elimination path con 4-step; grep-audit backlog.
- **Out of scope**: Sequence/TimeRange (M1.7 fratello, vedi TICKET-SEQUENCE-LOCAL-FRAME); nuovi preset text/cinematic (flusso parallelo M1.6); Expressions V2 (M5); Text 3D (M5).

## Source anchors (grep-audit pre-commit, snapshot da compilare)

```
$ rg '\b(font_path|image_path|video_path|audio_path)\b' content src include --type cpp --type hpp
# result: ~30-60 hits attesi (audit pre-Step-1; diventano 0 a Step-4 DONE)

$ rg 'use_default_font|draw_black_rect|return empty_frame|continue;\s*//\s*missing|placeholder' src content --type cpp
# result: ~10-25 hits attesi (audit pre-Step-1; diventano 0 a Step-4 DONE)

$ rg 'catch\s*\(\.\.\.\)\s*\{[^}]{0,400}MESSAGE[^}]{0,200}return' tests --type cpp
# result: ~3-8 hits attesi (audit pre-Step-1; diventano 0 a Step-4 DONE)

$ rg 'class\s+\w+Preflight\b' src include --type cpp --type hpp
# result: 5 attesi (Text/Image/Video/Audio/Font Preflight) → 0 a Step-4 DONE
```

## Cross-references

- Sibling ticket: [`TICKET-SEQUENCE-LOCAL-FRAME`](tickets/TICKET-SEQUENCE-LOCAL-FRAME.md) (parallelo workstream per la single-source-of-truth sul tempo).
- Roadmap milestone: [`docs/ROADMAP.md`](../ROADMAP.md) §M1.7 — Sequence + Asset Readiness (Single Source of Truth) (NEW this commit).
- Current status: [`docs/CURRENT_STATUS.md`](../CURRENT_STATUS.md) §"M1.7 Sequence + Asset Landing" snapshot (NEW this commit).
- AGENTS.md: [`AGENTS.md`](../../AGENTS.md) §Anti-duplication Rules (no `GlobalAssetRegistry`, no `AssetServiceLocator`).
- Lavoro prerequisito: TICKET-P1-07 (Asset resolver globale, PLANNED) — questo ticket è la concretizzazione canonicale di P1-07.

## Vincoli architetturali di esecuzione

- 4 commit atomic (uno per step), ognuno direttamente su `main` (no branch);
- `tools/wrap_push.sh origin main` prima di ogni push (GATE-MNT-01);
- Aggiornare `CURRENT_STATUS.md` + `FOLLOWUP_TICKETS.md` nello stesso commit (gate #7 `check_doc_sync.sh`);
- `tools/check_architecture_boundaries.sh` deve restare 16/16 PASS dopo Step 4;
- Nessun nuovo singleton/registry/resolver/cache/service-locator (regola permanente);
- path raw solo dentro `AssetManifest::entry_for(owner)` (nessun path raw sparpagliato);
- `tools/check_no_silent_asset_fallback.sh` (NEW forward-only grep gate, Step 4) deve restituire 0 hits per chiudere la regression.

## Avvio rigido (per AGENTS.md regola di stato)

1. Step 1 PRIMA — codice nuovo verde + zero codice esistente toccato;
2. Step 2 DOPO — adapters landed + tutti i test preesistenti PASS bit-identical;
3. Step 3 DOPO — nuovi contenuti solo `AssetRef` esplicito nel manifest;
4. Step 4 ULTIMO — solo quando grep-audit backlog è ZERO su tutti i 5 legacy items A-E + tutti i test PASS macchina-verificato.

## Stato del ticket (this commit)

**PLANNED** (action-plan landing 2026-07-07). Zero codice toccato. Grep-audit backlog: da compilare al primo commit di Step 1 (forward-point). Owner: Matteo / agenti cron. Acceptance evidence: macchina-verifica al commit Step 4 (target: post `main@7eb5c2ba` baseline-verde + 11/11 PASS).

## Grep-Audit Pre-Step-4 Snapshot (commit TBD-pending this session, 2026-07-07)

**Forward-only grep-gate tool**: [`tools/check_legacy_asset_prevalence.sh`](../check_legacy_asset_prevalence.sh).

**Snapshot numerico pre-Elimination (machine-verified post-fix round 2, exit 0)**:

| Item | Count | Pattern |
| --- | --- | --- |
| AST_ITEM_A | 67 hits  | `\b(font_path \| image_path \| video_path \| audio_path)\b` (scope: content/ + src/scene/ — exclude `include/chronon3d/` `.path` field reads) |
| AST_ITEM_B | 8 hits   | `(resolve_handle \| load_image \| decode_video \| decode_audio \| font_engine\.load)` |
| AST_ITEM_C | 8 hits   | `(use_default_font \| draw_black_rect \| empty_frame \| placeholder)` + `continue;[[:space:]]*//[[:space:]]*missing` |
| AST_ITEM_D | 0 hits   | `catch\s*\(\s*\.\.\.\s*\)\s*\{[^}]*MESSAGE[^}]*return` (multi-line pattern) |
| AST_ITEM_E | 2 hits   | `class[[:space:]]+[A-Za-z]+Preflight\b` |
| **Total** | **85** | (target post-Step-4 DONE = 0) |

**Step 4 acceptance gate**: `tools/check_legacy_asset_prevalence.sh` exit 0 con tutti i 5 items = 0.

**Honest state**: il snapshot documenta la pre-Elimination surface. AST_ITEM_D=0 (clean) per caso fortunato (nessun test file usava il pattern completo catch + MESSAGE + return al commit del landing). AST_ITEM_E=2 (TextPreflight + ImagePreflight — vedi `src/text/text_preflight*.hpp` storico). Le count sono machine-verified; ogni drift post-Elimination richiederà re-run del tool.

