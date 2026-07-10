# ADR-016 — Sequence + Asset canonical contract: TimelineResolver decide cosa esiste al frame, AssetPreflightResolver decide se gli asset sono pronti; RenderGraphBuilder riceve scena già risolta; Renderer non inventa timeline né asset

| Field | Value |
|---|---|
| **Status** | 📋 Documented (Step 1/4 landed su `main@33798b0a` per entrambi i workstream Sequence + Asset; Step 2/3/4 forward-point. Acceptance evidence a `DONE` richiederà Step 4 macchina-verifica: post `main@7eb5c2ba` baseline-verde + 11/11 PASS + grep-audit backlog = 0 su tutti i 10 legacy items combinati) |
| **Date** | 2026-07-07 |
| **Deciders** | M1.7 action-plan owner (chronon3d Sequence + Asset Readiness single-source-of-truth workstream) |
| **Tags** | `sequence`, `asset-readiness`, `canonicalisation`, `single-source-of-truth`, `M1.7`, `safe-migration`, `anti-duplication`, `SDK-surface`, `legacy-retire`, `timeline-v2`, `asset-v2` |
| **Related** | [ADR-011 — camera-legacy-deletion](./ADR-011-camera-legacy-deletion.md) (the canonicalisation close-out precedent per camera_v1 — stessa filosofia, dominio camera); [ADR-013 — camera_v1-semantics](./ADR-013-camera-v1-semantics.md) (Decision 5 "default-then-promotion" pattern, the architectural precedent for "build explicit invariants in the producer, document them, lock with tests"); [ADR-015 — screen-space-trs-invariant](./ADR-015-screen-space-trs-invariant.md) (the producer-owns-invariant pattern ripreso da Decision 2 di questo ADR); [ADR-005 — asset-resolver-local](./ADR-005-asset-resolver-local.md) (the engine-local AssetResolver che `chronon3d::AssetRegistry` di `asset_registry.hpp` canonizza — la base che `AssetPreflightResolver` astrae); [`docs/tickets/TICKET-SEQUENCE-LOCAL-FRAME.md`](../tickets/TICKET-SEQUENCE-LOCAL-FRAME.md) (5 legacy items Sequence A-E + 4-step migration plan); [`docs/tickets/TICKET-ASSET-READINESS.md`](../tickets/TICKET-ASSET-READINESS.md) (5 legacy items Asset A-E + 4-step migration plan); `docs/FOLLOWUP_TICKETS.md` §M1.7 P1 backlog rows (2 ticket entrambi `PARTIAL (Step 1/4 DONE)`); `docs/ROADMAP.md` §M1.7 milestone; `docs/CURRENT_STATUS.md` §M1.7 paragraph bumped; `docs/CHANGELOG.md` 2 entries (Luglio 2026); `include/chronon3d/timeline/timeline_resolver_v2.hpp` (Step 1 Sequence: 4 simboli canonici landed); `include/chronon3d/timeline/legacy_adapters.hpp` (Step 2 Sequence: 3 adapters landed); `include/chronon3d/assets/asset_readiness_v2.hpp` (Step 1 Asset: 5 simboli canonici landed); `tools/check_legacy_timeline_prevalence.sh` + `tools/check_legacy_asset_prevalence.sh` (2 forward-only grep-gate tools, pre-Elimination snapshot 254 + 85 = 339 hits); `docs/tickets/TICKET-ASSET-READINESS.md` §Grep-Audit Pre-Step-4 Snapshot; AGENTS.md v0.1 §"Feature Freeze — REVOCATO" + §"Regole di lavoro" + §"Anti-duplication Rules" + §"Piano operativo canonico"; `tools/wrap_push.sh` (GATE-MNT-01 push-side wrapper); `tools/check_doc_sync.sh` (gate #7 — 4 doc canonical files all updated in each commit). |

## Context

Chronon3d's authoring pipeline has accumulated **two parallel SSoT violations** in M1.7:

### Violation 1 — Timeline non canonicalizzata (5 legacy items A-E)

La **decisione di cosa esiste al frame globale** (`SequenceNode attivo?` + `local_frame da usare?` + `range attivo?`) è attualmente frammentata in 5 punti:

* **A.** `if (frame ...)` sparsi nei content (es. `l.text(...)` con `frame>=start && frame<end`) — **151 hits** in `content/ + src/scene/ + src/render_graph/`.
* **B.** Animator/sampler che legge `ctx.frame` / `global_frame` invece di `local_frame` — **39 hits** in `content/ + src/text/ + src/animation/`.
* **C.** `Layer.from / Layer.duration` decisi dal render graph (transition node, precomp, video source) invece che dal sequence resolver — **1 hit** in `src/render_graph/`.
* **D.** `duration = 0/1` (numeri magici) usati come trucco "statico" per saltare l'evaluation dell'animator — **52 hits** in `content/ + src/scene/`.
* **E.** 5 coordinate temporali duplicate (`composition_frame / layer_frame / sequence_frame / animator_frame / source_frame`) calcolate in posti diversi — **11 hits** in `content/ + src/`.

Totale pre-Elimination snapshot: **254 hits** (vedi `docs/tickets/TICKET-SEQUENCE-LOCAL-FRAME.md` §Grep-Audit Pre-Step-4 Snapshot).

### Violation 2 — Asset readiness non canonicalizzata (5 legacy items A-E)

La **decisione di se gli asset sono pronti** (`font disponibile?` + `image caricata?` + `video decodificabile?` + `audio decodificabile?`) è attualmente frammentata in 5 punti:

* **A.** Path raw sparsi senza `AssetRef` (es. `std::string font_path{"assets/fonts/Poppins-Bold.ttf"}` in `CenterTextOptions`) — **67 hits** in `content/ + src/scene/`.
* **B.** Asset discovery render-time (`rctx.text_resources->resolve_handle(...)` dentro `prepare_text_run()`; `image_loader.cpp`; `video_decoder_*`; `audio_decoder_*`; `font_engine.load`) — **8 hits** in `src/ + content/`.
* **C.** Fallback silenziosi (`use_default_font`, `draw_black_rect`, `return empty_frame`, `placeholder`, `continue;//missing`) — **8 hits** in `src/ + content/`.
* **D.** `catch(...) { MESSAGE ... return }` nei test readiness — **0 hits** (clean per caso fortunato al commit del landing; `TICKET-ASSET-READINESS.md` §Grep-Audit documenta).
* **E.** Asset validation duplicata per-feature (`TextPreflight / ImagePreflight / VideoPreflight / AudioPreflight / FontPreflight`) — **2 hits** in `src/` (TextPreflight + ImagePreflight storici).

Totale pre-Elimination snapshot: **85 hits** (vedi `docs/tickets/TICKET-ASSET-READINESS.md` §Grep-Audit Pre-Step-4 Snapshot).

### Grand total: 254 + 85 = 339 hits pre-Elimination, target post-Step-4 = 0

### Filosofia architetturale violata

Questa duplicazione viola la regola **AGENTS.md v0.1 §"Anti-duplication Rules"**:

> *No duplicare registry, resolver, sampler, cache, service locator o checklist.*

Più specificamente, queste due duplicazioni sono figlie di **tre "regole finali" non scritte ma implicite** che questo ADR codifica:

1. **`TimelineResolver` decide cosa esiste al frame globale** — non il render graph (transition + precomp + video source) che ri-applica `Layer.from/.duration` per conto suo, né il content (frame-if sparsi) che decide localmente "questa frame è dentro il mio range?".
2. **`AssetPreflightResolver` decide se gli asset sono pronti** — non il render (resolve_handle in prepare_text_run) che inventa fallback al volo, né l'engine (`rctx.text_resources->...`) che restituisce error silenzioso.
3. **`Renderer` non inventa timeline né asset** — niente skip su frame `frame < layer.from`, niente `use_default_font / draw_black_rect / empty_frame` su missing. PNG scuri vietati (vedi `chronon3d_install_consumer_tests` Phase 4 acceptance gate).

Queste tre regole sono il **contratto canonico** che questo ADR formalizza come design decision unificata.

## Decision

Questo ADR codifica 4 decisioni di design che insieme formano il **contratto SSoT per timeline + asset readiness** del Chronon3d authoring pipeline.

### Decision 1 — Sequence V2 contract: 4 simboli canonici in `chronon3d::timeline::v2` (Step 1 landed)

Il **contratto Sequence V2 single-source-of-truth** è:

| Symbol | Type | Role |
|---|---|---|
| `TimeRange` | `struct { Frame from; Frame duration; }` POD | Coppia canonica `{ from, duration }` per definire l'intervallo attivo di un nodo timeline. Sostituisce il pattern `frame >= start && frame < end` sparso. Tratta `duration < 0` come sentinel "infinito" (sempre attivo dopo `from`). |
| `SequenceNode` | `struct { std::string name; TimeRange range; std::function<void(SequetteBuilder&)> build; }` | Nodo timeline canonico. `name` = identificatore logico; `range` = `TimeRange` canonica; `build` = callback opzionale che popola i layer della sequence quando attiva (Step 3 popolerà `SequenceBuilder` come full type esposto; Step 1 = empty placeholder ABI-stable). |
| `TimelineSampleContext` | `struct { Frame global_frame; Frame local_frame; Frame sequence_start; float fps; }` POD | Unico value-type che descrive la situazione di sampling temporale. Sostituisce le 5 coordinate temporali duplicate (composition / layer / sequence / animator / video source frame). `local_frame = global_frame - sequence_start` quando la sequence è attiva (clamp 0 prima del range). |
| `TimelineResolver` | `class { ResolvedScene resolve(const SceneDescriptor&, Frame, float) const; static constexpr Frame make_local_frame(Frame, Frame) noexcept; }` | SSoT per "cosa esiste al frame globale". Header-only inline, no singleton/registry/cache. `make_local_frame` constexpr friendly. `resolve` ritorna `ResolvedScene{composition_range, active_sequences, sample}` con invariante `local_frame = global_frame - sequence_start` (clamp 0). |

Più i 3 helper POD: `SceneDescriptor` (input: `composition_range + sequences[]`), `ResolvedScene` (output: `composition_range + active_sequences + sample`), `SequenceBuilder{}` (Step 1 empty placeholder; Step 3 ridefinirà full body).

**Namespace**: `chronon3d::timeline::v2` (distinto da `chronon3d::SequenceContext` esistente in `include/chronon3d/timeline/sequence.hpp` per evitare ogni clash di simboli omonimi). I 4 simboli + 3 helper POD coesistono con la `chronon3d::SequenceContext` esistente senza collisioni (namespace-disjoint).

**Source**: `include/chronon3d/timeline/timeline_resolver_v2.hpp` (Step 1 landed `main@0f47d591`).

### Decision 2 — Asset V2 contract: 5 simboli canonici in `chronon3d::assets::v2` (Step 1 landed)

Il **contratto Asset V2 single-source-of-truth** è:

| Symbol | Type | Role |
|---|---|---|
| `AssetKind` | `enum class : unsigned char { Font, Image, Video, Audio }` | Discriminator 4 valori (1 byte). Distinto da `chronon3d::AssetType` esistente in `asset_metadata.hpp` (5 valori inclusi Mesh + Unknown, semantica metadata/registry); valori intenzionalmente non-allineati per prevenire `static_cast` cross-enum accidentale. |
| `AssetRef` | `struct { AssetKind kind; std::string path; std::string owner; bool required{true}; }` POD | Reference canonico a un asset richiesto da una scena. `owner` = identificatore logico del proprietario (es. "LightPulse/text/label"); `required` = hard/soft flag. |
| `AssetManifest` | `class { void add(AssetRef); std::optional<Entry> entry_for(const std::string&) const noexcept; std::vector<AssetRef> all() const; std::size_t size() const noexcept; bool empty() const noexcept; }` | Value type NON singleton. `add(AssetRef)` + `entry_for(owner)` O(1) via `unordered_map` + `all()` insertion order. `Entry = AssetRef` (type alias). first-inserted-wins su duplicato (`std::unordered_map::emplace` su chiave esistente è no-op dal C++11, preserva l'originale). |
| `AssetPreflightResult` | `struct { bool ok{true}; std::vector<AssetRef> missing{}; }` POD | Output del preflight. `ok = true` se TUTTI gli asset richiesti sono disponibili; `missing` lista gli `AssetRef` non disponibili (vuota se `ok = true`). |
| `AssetPreflightResolver` | `class { AssetPreflightResult preflight(const AssetManifest&) const noexcept; }` | SSoT per "gli asset sono pronti?". Header-only inline, no singleton/registry/cache. Step 1 = no-op `check_*` stubs `return true;` (Step 3 popolerà la logica reale: file exists + readable + kind/extension match). |

**Namespace**: `chronon3d::assets::v2` (distinto da `chronon3d::AssetType` esistente in `asset_metadata.hpp` + `chronon3d::AssetRegistry` esistente in `asset_registry.hpp` per evitare ogni clash di simboli omonimi). I 5 simboli coesistono con l'`AssetRegistry` esistente senza collisioni (namespace-disjoint, semantica complementare: `AssetRegistry` = metadata/registry; `AssetManifest` = owner-centric declaration).

**Source**: `include/chronon3d/assets/asset_readiness_v2.hpp` (Step 1 landed `main@33798b0a`).

### Decision 3 — Flow pattern canonico: 4 attori, 3 regole finali, 1 chain di ownership

Il **flow pattern canonico** che questo ADR codifica è un chain a 4 attori con 3 regole finali di ownership:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  Authoring time (SceneBuilder)                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │ s.sequence("intro", TimeRange{.from=0, .duration=60}, [&](SeqBuilder& sb) {   │
│  │   sb.layer("title", [](LayerBuilder& l) {                            │   │
│  │     l.add(AssetRef{AssetKind::Font, "fonts/Poppins-Bold.ttf",        │   │
│  │                    "LightPulse/text/label", true});  ←─ AssetManifest│   │
│  │     l.text("t", {.content="Hello, World!"});                        │   │
│  │   });                                                                │   │
│  │ });                                                                  │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│         │                                                                   │
│         ▼                                                                   │
│  ┌────────────────┐    ┌──────────────────────┐                            │
│  │ SceneDescriptor│    │   AssetManifest      │                            │
│  │  (Sequence V2) │    │  (Asset V2)          │                            │
│  └────────────────┘    └──────────────────────┘                            │
│         │                       │                                           │
│         ▼                       ▼                                           │
│  ┌─────────────────┐    ┌────────────────────────┐                         │
│  │ TimelineResolver│    │ AssetPreflightResolver │                         │
│  │ .resolve(scene, │    │ .preflight(manifest)   │                         │
│  │   frame, fps)   │    │   → ok=true / missing[]│                         │
│  │   → ResolvedScene│   └────────────────────────┘                         │
│  │   {active[], sample}                                                    │
│  └─────────────────┘                                                       │
│         │                                                                   │
│         ▼                                                                   │
│  ┌──────────────────────────────┐                                          │
│  │ RenderGraphBuilder           │ ← Riceve scene GIÀ RISOLTA              │
│  │ (riceve ResolvedScene)       │   (no skip-on-frame, no fallback)         │
│  └──────────────────────────────┘                                          │
│         │                                                                   │
│         ▼                                                                   │
│  ┌──────────────────────────────┐                                          │
│  │ Renderer                     │ ← NON inventa timeline né asset        │
│  │ (esegue la scena risolta)   │   (no skip, no fallback, no PNG scuri)   │
│  └──────────────────────────────┘                                          │
└─────────────────────────────────────────────────────────────────────────────┘
```

**3 regole finali (vincolanti per Chiusura M1.7)**:

1. **`TimelineResolver` decide cosa esiste al frame globale** — produce `ResolvedScene{active_sequences[], sample{global_frame, local_frame, sequence_start, fps}}` con `active_sequences` già filtrate sul `global_frame` arg. Il render graph riceve scena GIÀ RISOLTA (no skip-on-frame, no `frame < layer.from` check nel graph).
2. **`AssetPreflightResolver` decide se tutti gli asset sono pronti** — produce `AssetPreflightResult{ok=true/false, missing[]}`. `RenderJob::start()` chiama preflight UNA volta prima del render loop (Step 3); se `ok = false`, FAIL esplicito con messaggio per ogni `missing`: `FONT missing  path: ...  owner: ...`. Engine + render non fanno asset discovery al volo.
3. **`Renderer` non inventa timeline né asset** — niente skip su frame `frame < layer.from` (timeline non è responsabilità sua); niente `use_default_font / draw_black_rect / empty_frame` su missing asset (asset fallback non è responsabilità sua). PNG scuri vietati in `chronon3d_install_consumer_tests` Phase 4 (acceptance gate).

Queste 3 regole sono il **contratto canonico** che questo ADR formalizza. Sono interdipendenti: rimuoverne una rompe le altre due (es. se il render graph salta frame, il preflight asset diventa inutile perché la scena è già parzialmente filtrata; se il render inventa fallback asset, il preflight diventa inutile perché ok=true viene ignorato).

### Decision 4 — Anti-duplication enforcement: ZERO nuovi singleton/registry/cache/service-locator

Il **vincolo architetturale** (AGENTS.md v0.1 §"Regole di lavoro" + §"Anti-duplication Rules") che questo ADR ribadisce come permanente per il dominio timeline + asset:

* `TimelineResolver` è una **value type stateless** (header-only inline, no cache, no singleton). Può essere conservata in `RenderSession` (post-Step-3) o in `RenderGraphContext` senza dover introdurre nessun nuovo global registry.
* `AssetManifest` è una **value type** con `std::vector<AssetRef>` + `std::unordered_map<std::string, AssetRef>` interni. NON singleton. NON globale. NON thread-local.
* `AssetPreflightResolver` è una **value type stateless** con `preflight()` const member function. NON singleton. NON globale. NON thread-local.
* `TimelineSampleContext` + `AssetPreflightResult` sono **POD struct** per-value return. NON alloccano, NON hanno ownership.
* NO `GlobalTimeline` / `SequenceRegistry` / `AssetServiceLocator` / `PreflightCache` (vietati per nome da AGENTS.md §"Anti-duplication Rules").

Il pattern `AssetManifest` + `AssetPreflightResolver` (manifest esplicito passato al preflight) **non è** un singleton o service-locator: il manifest è un value-type costruito al call site, il preflight è un value-type che opera su di esso via const-ref. Stesso pattern di `std::find` (algorithm) + `std::vector<T>` (container): nessun singleton, nessun registry.

## Migration path (4-step safe migration — Decision 5)

Per Marcuss-ops "Non eliminare subito tutto fisicamente" (sezione 4 del piano utente), la canonicalizzazione timeline + asset è eseguita via **4-step safe migration** per ticket. Entrambi i workstream (Sequence + Asset) seguono lo stesso pattern, paralleli su commit separati.

### Sequence 4-step (TICKET-SEQUENCE-LOCAL-FRAME)

| Step | Work | Commit | State |
|---|---|---|---|
| **Step 1** | Aggiungi 4 simboli canonici (`TimeRange` + `SequenceNode` + `TimelineResolver` + `TimelineSampleContext`) in `chronon3d::timeline::v2` namespace. Zero codice esistente toccato. | `main@0f47d591` (post-rebase) | ✅ DONE |
| **Step 2** | Legacy adapters (`Layer.from/.duration → SequenceNode` wrapper + `FrameContext → TimelineSampleContext` compat adapter) in `chronon3d::timeline::v2`. AGGIUNTI ma NON chiamati dal render graph / scene builder. Tutti i test preesistenti PASS bit-identical. | `main@fab2046e` | ✅ DONE |
| **Step 3** | Nuovi content usano SOLO `s.sequence("intro", TimeRange, build_callback)` (almeno 5 scene nuove in `content/showcases/` o `content/experimental/ae-parity/`). `SequenceBuilder` viene ridefinito come full type esposto con le API per popolare i layer. | forward-point | 📋 PLANNED |
| **Step 4** | Elimina legacy: rimuovi il branch `if (frame < layer.from || frame >= layer.from + layer.duration) skip layer` dal render graph; rimuovi i `Layer.from / Layer.duration` come campi dal modello dati (diventano unused); rimuovi i `if frame…` sparsi nei content già migrato; rimuovi l'adapter monolitico di compatibilità `ctx.frame → TimelineSampleContext{local=ctx.frame}`. Grep-audit backlog Sequence = 0. | forward-point | 📋 PLANNED |

### Asset 4-step (TICKET-ASSET-READINESS)

| Step | Work | Commit | State |
|---|---|---|---|
| **Step 1** | Aggiungi 5 simboli canonici (`AssetKind` + `AssetRef` + `AssetManifest` + `AssetPreflightResult` + `AssetPreflightResolver`) in `chronon3d::assets::v2` namespace. Zero codice esistente toccato. | `main@33798b0a` | ✅ DONE |
| **Step 2** | Legacy adapters (`font_path / image_path / video_path / audio_path` raw sparsi in `CenterTextOptions` etc. → wrapper che crea `AssetRef` + lo aggiunge al `AssetManifest` della scena a startup; bridge `rctx.text_resources->resolve_handle(...)` error → `AssetPreflightResult::missing` esplicito). Tutti i test preesistenti PASS bit-identical. FNV-1a cache key invariato. | forward-point | 📋 PLANNED |
| **Step 3** | Nuovi content usano SOLO `AssetRef{kind, path, owner, required}` esplicito nel manifest della scena. `RenderJob::start()` chiama `AssetPreflightResolver::preflight(manifest)` UNA volta prima del render loop. `check_font / check_image / check_video / check_audio` popolate con logica reale (file exists + readable + kind/extension match). | forward-point | 📋 PLANNED |
| **Step 4** | Elimina legacy: rimuovi `TextPreflight / ImagePreflight / VideoPreflight / AudioPreflight / FontPreflight` consolidati in `AssetPreflightResolver`; rimuovi i fallback silenziosi da `image_loader.cpp / video_decoder_* / audio_decoder_* / font_engine.cpp`; rimuovi i path raw in `CenterTextOptions / SceneBuilder::image(...) / video(...) / audio(...)` API comoda; sostituisci i test readiness `catch(...) { MESSAGE; return; }` con `FAIL("Render failed: " << e.what())` esplicito. Grep-audit backlog Asset = 0. `tools/check_no_silent_asset_fallback.sh` (NEW forward-only grep gate) restituisce 0 hits. | forward-point | 📋 PLANNED |

### Acceptance criteria per M1.7 DONE (entrambi i workstream)

* **Step 4 macchina-verifica**: post `main@7eb5c2ba` baseline-verde + 11/11 PASS + `bash tools/check_legacy_timeline_prevalence.sh` exit 0 con tutti i 5 Sequence items = 0 + `bash tools/check_legacy_asset_prevalence.sh` exit 0 con tutti i 5 Asset items = 0.
* **Nessun nuovo singleton/registry/resolver/cache/service-locator** (regola permanente AGENTS.md §"Anti-duplication Rules").
* **PNG scuri vietati** in `chronon3d_install_consumer_tests` Phase 4 (asset fallback vietato → preflight FAIL → no PNG scuri).
* **Boundary gate `tools/check_architecture_boundaries.sh` deve restare 16/16 PASS** dopo Step 4 (zero public API surface regression).

## Consequences

### Positive

* **Single source-of-truth per timeline + asset**: `TimelineResolver` + `AssetPreflightResolver` sono i 2 unici resolver per "cosa esiste al frame" + "gli asset sono pronti". `RenderGraphBuilder` riceve scena già risolta. `Renderer` non inventa. Le 3 regole finali di Decision 3 sono il **contratto canonico** che ogni nuovo modulo deve rispettare.
* **Anti-duplication enforcement esplicito**: Decision 4 ribadisce come permanente il vincolo "NO nuovi singleton/registry/cache/service-locator" per i domini timeline + asset. `SequenceRegistry` / `GlobalTimeline` / `AssetServiceLocator` / `PreflightCache` vietati per nome.
* **Grep-audit backlog misurabile**: i 2 forward-only grep-gate tools `tools/check_legacy_timeline_prevalence.sh` + `tools/check_legacy_asset_prevalence.sh` danno il count pre-Elimination (339 hits) e il target post-Step-4 (= 0). Il progresso della canonicalizzazione è machine-verified, non claimed.
* **Producer-owns-invariant pattern riusato**: Decision 1 di ADR-015 (resolver scrive il canonical screen-space TRS nei campi `transform.scale.z / rotation / anchor`) è lo stesso pattern di Decision 2 di questo ADR (`AssetPreflightResolver` scrive `ok = missing.empty()` invece di lasciare al consumer il compito di controllare). Il pattern è riusabile per future invarianti simili.
* **AGENTS.md v0.1 Cat-1/Cat-3/Cat-5 freeze-compliant**: 9 nuovi simboli pubblici canonici (4 Sequence + 5 Asset) landed via Step 1+2. ZERO nuovi singleton/registry/cache/service-locator. ABI pubblico espanso di 9 free POD/aggregate types (footprint incrementale ~270B per scena, trascurabile). Test preesistenti bit-identical garantiti (gli adapter sono AGGIUNTI ma NON chiamati).
* **Reconciliation-friendly con commit paralleli**: i 2 namespace `chronon3d::timeline::v2` + `chronon3d::assets::v2` sono disgiunti dall'esistente `chronon3d::SequenceContext` (in `sequence.hpp`) + `chronon3d::AssetType`/`chronon3d::AssetRegistry` (in `asset_metadata.hpp`/`asset_registry.hpp`). Il parallel commit `a0fbc57b` (SequenceBuilder facade + nested sequence support in `chronon3d::SequenceNode` + `chronon3d::TimelineResolver` top-level) coesiste senza collisioni; la reconciliation finale (Step 4 o ADR separato) può decidere quale design diventa canonico.

### Negative / Migration cost

* **Step 3 + Step 4 ancora forward-point**: il lavoro di migration effettiva (5+ scene nuove su `s.sequence(...)` + `AssetRef` esplicito; rimozione fisica di `Layer.from/.duration` + `TextPreflight/etc` + fallback silenziosi) è un multi-PR bounded. ~339 grep hits da eliminare uno per uno, con rischio di regressione per call site che il grep non cattura (semantic-equivalence, non text-equivalence).
* **Reconciliation debt con commit paralleli**: `a0fbc57b` (SequenceBuilder facade + nested) + i miei 3 commit `0f47d591` / `fab2046e` / `33798b0a` (flat design) coesistono su main. La reconciliation (quale design diventa canonico, quale diventa facade aggiuntivo) è una decisione di design non ancora presa. Fino a quella decisione, il codice ha 2 implementazioni di TimelineResolver / SequenceNode in 2 namespace separati.
* **Runtime verification del forward-check TU deferred**: il forward-check TU di Step 1 Sequence (`/tmp/_step1_consumer_check.cpp`) + Step 2 Sequence (`/tmp/_step2_consumer_check.cpp`) hanno typecheck-only verification (il runtime execute è limitato dal toolchain `/tmp/` standalone; richiede `Layer.cpp` + transitive deps). Step 1 Asset (`/tmp/_step1_asset_consumer_check.cpp`) ha full compile+run verification. La promozione a `DONE` richiederà Step 4 macchina-verifica su host con build funzionante.
* **`SequenceBuilder{}` empty placeholder Step 1 = ABI-stable ma inutilizzato**: Step 1 definisce `SequenceBuilder` come empty struct (1 byte, ABI-stable). Step 3 ridefinirà come full type con le API per popolare i layer. Fino a Step 3, il `build` callback di `SequenceNode` non può essere invocato utilmente (passa un `SequenceBuilder&` vuoto). Il rischio è che un futuro consumer chiami `node.build(sb)` con `sb` vuoto aspettandosi un side-effect — Step 1 non previene questo, si affida alla review del code-reviewer per catcharlo.

### Neutral

* `chronon3d::SequenceContext` (in `include/chronon3d/timeline/sequence.hpp`) resta invariato e operativo fino a Step 4. Convive con `chronon3d::timeline::v2::SequenceNode` (namespace-disjoint). Nessuna sostituzione forzata.
* `chronon3d::AssetType` (in `asset_metadata.hpp`) + `chronon3d::AssetRegistry` (in `asset_registry.hpp`) restano invariati e operativi fino a Step 4. `AssetManifest` è complementare (manifest owner-centric vs registry metadata-centric). Step 3 popolerà il mapping `AssetKind → AssetType` canonico per il wiring.
* `chronon3d::Composition` (in `composition.hpp`) resta intatto e rimane l'input della pipeline legacy fino a Step 4. `SceneDescriptor` (V2) è un PODO minimale (`composition_range + sequences[]`); `chronon3d::Composition` esistente ha più field (frame_rate, asset_registry, etc.). Il wiring `chronon3d::Composition → chronon3d::timeline::v2::SceneDescriptor` arriverà a Step 2/3.

## Decision 6 — Reconciliation of parallel Sequence designs (Addendum, post-rebase `main@0481a1ce`)

**Context.** La safe migration di `TICKET-SEQUENCE-LOCAL-FRAME.md` ha prodotto 4 simboli canonici + 3 legacy adapters in `chronon3d::timeline::v2::*` (commits `0f47d591` + `fab2046e`), namespace-disjoint dall'esistente `chronon3d::SequenceContext`. Parallelamente, il commit lineage `a0fbc57b` (integrato su `main` dai commit `7a4b5a6e` + `81af2949` + `4a874219` di `origin/main`) ha sviluppato un design canonico top-level integrato in `SceneBuilder::sequence()` + `tests/core/timeline/test_sequence_builder.cpp` con `SequenceNode` **recursive tree** (children = nested sequences), `TimelineResolver` **static recursive walker** + `SequenceBuilder` **full type facade** (180+ LOC). Il rebase di questo commit su `main` ha prodotto una **doppia canonical architecture** tecnicamente clean (namespace disgiunti) ma architetturalmente in violazione di AGENTS.md v0.1 §"Anti-duplication Rules" (2x `SequenceNode`, 2x `TimelineResolver`, 2 pattern `SequenceBuilder`).

**Decisioni (5 sub-decisioni vincolanti)**:

1. **Decision 6.1 — Parallel commit canonical = SSoT.** Il design top-level (`chronon3d::SequenceNode` recursive tree + `chronon3d::SequenceRange::trim_before` + `chronon3d::TimelineResolver::resolve(roots, ctx) -> vector<Resolution>` + `chronon3d::SequenceBuilder` facade integrato in `SceneBuilder::sequence()` via `if constexpr (std::is_invocable_v<Fn, SequenceBuilder&>)`) è **dichiarato canonico**. Razionale: (a) integrazione reale con `SceneBuilder` + 10+ test case + nested sequence support con `trim_before` (modello flat di v2 non gestisce nesting); (b) coverage già verificata macchina; (c) `Resolution{active_path + effective_context}` output è semanticamente più ricco del `ResolvedScene{composition_range + active_sequences + sample}` flat di v2.

2. **Decision 6.2 — Namespace v2 deprecated ex-post.** `chronon3d::timeline::v2::*` (committato in `0f47d591` + `fab2046e`) è designato per rimozione fisica. Il namespace è già schedulato per l'eliminazione allo **Step 3 PIVOT** del `TICKET-SEQUENCE-LOCAL-FRAME` (vedi Decision 6.5 sotto). Fino a quel momento, il namespace v2 rimane **funzionalmente attivo ma non canonico** (i consumer esistenti che lo importano continuano a compilare grazie al namespace-disjoint, ma i nuovi content devono usare il canonical top-level).

3. **Decision 6.3 — `TimelineSampleContext` promosso a top-level (unica innovazione utile del namespace v2).** Il POD `chronon3d::timeline::v2::TimelineSampleContext{global_frame, local_frame, sequence_start, fps}` è l'unico simbolo del namespace v2 che NON ha un equivalente diretto nel top-level canonical (`FrameContext` esistente è pesante; `Resolution::effective_context` è accoppiato alla resolution). **Promozione**: aggiungere `chronon3d::TimelineSampleContext` come POD top-level pubblico, equivalente verbatim al v2 ma in `chronon3d::*` namespace. Implementazione target: `include/chronon3d/timeline/timeline_sample_context.hpp` (NUOVO header, registrato in `cmake/Chronon3DPublicHeaders.cmake`), con advertisement che diventa il sampler-context canonico per `Animator::sample(global_frame, local_frame, fps)` downstream.  <!-- drift-allow: stale-ref -->

4. **Decision 6.4 — I 3 legacy adapters namespace v2 migrati al canonical top-level.** Gli adapters `is_active(TimeRange, Frame)` + `make_sequence_from_layer(const Layer&) -> SequenceNode` + `make_sample_context(const FrameContext&) -> TimelineSampleContext` diventano **top-level helpers**: `is_active(SequenceRange, Frame) noexcep`t (adattato al `SequenceRange` canonical con `trim_before`), `Chronon3d::SequenceNode make_sequence_node_from_layer(const chronon3d::Layer&)` (Layer → top-level `SequenceNode` con `range: SequenceRange{from, duration, trim_before=0}`), `chronon3d::TimelineSampleContext make_sample_context(const chronon3d::FrameContext&)` (con la **promozione** `local_frame = global_frame - sequence_start` semantics che Step 3 del flat design aveva rimandato, ora possibile perché `FrameContext::frame` può essere confrontato con `SequenceRange::from`).

5. **Decision 6.5 — Step 3 PIVOT: include namespace v2 removal come pre-subtask.** Il `TICKET-SEQUENCE-LOCAL-FRAME.md` §Stato (Stato-revisiting 2026-07-08) viene aggiornato per riflettere che **Step 3 include un pre-subtask obbligatorio di rimozione fisica** di `include/chronon3d/timeline/timeline_resolver_v2.hpp` + `include/chronon3d/timeline/legacy_adapters.hpp` + de-registration da `cmake/Chronon3DPublicHeaders.cmake`. La rimozione è bit-identical-safe perché (a) nessun consumer interno importa gli header v2 (sono ADDED ma NON chiamati dal render graph / scene builder / composition); (b) i 3 free functions namespace v2 vengono preventivamente promossi a top-level (`is_active` + `make_sequence_node_from_layer` + `make_sample_context` come da Decision 6.4) prima della rimozione v2, così nessuna funzionalità viene persa.

**Pattern architetturale permanente**: è il pattern "**namespace-disjoint deprecation**" — il nuovo sistema (namespace v2) viene introdotto come superficie speculativa sicura; quando il design canonico parallelo matura, gli elementi utili vengono promossi a top-level e il namespace speculativo viene rimosso fisicamente. Pattern riusabile per future safe migrations nello stesso repository (es. M2.0 cache-key rework, M3.0 scene-graph-ecc).

**Cross-references aggiuntivi (post-Decision 6)**:

* `include/chronon3d/timeline/sequence_node.hpp` (top-level canonical `chronon3d::SequenceNode` recursive tree + `chronon3d::SequenceRange` + `chronon3d::ResolvedSequence`).
* `include/chronon3d/timeline/timeline_resolver.hpp` (top-level canonical `chronon3d::TimelineResolver` static recursive walker).
* `include/chronon3d/scene/builders/sequence_builder.hpp` (top-level canonical `chronon3d::SequenceBuilder` facade integrato in SceneBuilder).
* `include/chronon3d/scene/builders/detail/scene_builder_sequences.inl` (SceneBuilder::sequence auto-dispatch con `if constexpr` su `SequenceBuilder&` lambda).
* `tests/core/timeline/test_sequence_builder.cpp` (10+ test case coverage del top-level canonical).
* `docs/tickets/TICKET-SEQUENCE-LOCAL-FRAME.md` §Stato (revisiting 2026-07-08 con pivot architetturale).
* `include/chronon3d/timeline/timeline_sample_context.hpp` (NEW header per `chronon3d::TimelineSampleContext` POD, target post-Addendum Step 4 promotion).  <!-- drift-allow: stale-ref -->
* `docs/baselines/main-acf7d1de-baseline.md` (re-verify grep-audit post-Step-2 freshness, 2026-07-08 baseline).

## Alternatives considered

* **A. Mantenere la duplicazione legacy + grep-gate come unica enforcement.** Rifiutato — la duplicazione legacy è figlia di 5+5 punti di decisione che producono behavior inconsistente (skip-on-frame, fallback silenziosi, catch-MESSAGE-return nei test). Il grep-gate è uno strumento di misura, non di fix. Decision 1+2+3+4 di questo ADR è il **fix**.
* **B. Single-resolver unificato `SceneResolver` che decide sia timeline che asset in un colpo.** Considerato per ridurre il numero di resolver; rifiutato perché i 2 domini hanno semantica ortogonale (timeline = tempo, asset = risorse esterne). Il preflight asset deve girare **prima** del render loop (FAIL-FAST) mentre la timeline resolve **durante** il render (per ogni frame). Il coupling forzato produrrebbe un resolver con 2 modalità di esecuzione e lifecycle. Decision 3 esplicita i 2 resolver separati con chain chiara.
* **C. `TimelineResolver` come singleton + `AssetPreflightResolver` come singleton (NO namespace v2).** Rifiutato per AGENTS.md §"Anti-duplication Rules" + Decision 4 di questo ADR. I singleton sarebbero esattamente il tipo di duplicazione che il design combatte.
* **D. Estendere `chronon3d::SequenceContext` esistente invece di creare `chronon3d::timeline::v2::SequenceNode`.** Considerato per ridurre il numero di tipi; rifiutato perché (1) `SequenceContext` ha un'API diversa (helper `sequence(ctx, from, duration)` che ritorna un contesto) vs il design V2 che è data-driven (`SequenceNode` con `name + range + build`); (2) `SequenceContext` è namespace `chronon3d` (top-level), `SequenceNode` V2 è namespace `chronon3d::timeline::v2`; il conflict sarebbe un break ABI. Step 4 reconciliation deciderà quale diventa canonico.
* **E. Step 1 = landing dei 9 simboli in UN solo header `readiness_v2.hpp` invece di 2 separati (timeline + assets).** Rifiutato perché i 2 domini sono ortogonali (timeline = tempo, asset = risorse) e 2 header separati riflettono l'orthogonalità. Inoltre 2 header = 2 commit atomici = doc-sync canonico indipendente.
* **F. Step 1 Sequence + Step 1 Asset landed nello stesso commit.** Rifiutato per AGENTS.md §"Regole di lavoro" "PR piccole e mirate, senza mescolare refactor indipendenti". I 2 commit separati (`0f47d591` per Sequence + `33798b0a` per Asset) riflettono i 2 ticket canonici distinti (TICKET-SEQUENCE-LOCAL-FRAME + TICKET-ASSET-READINESS).
* **G. `AssetManifest::Entry` come type alias vs type definition.** Scelto alias (`using Entry = AssetRef;`) per semplicità + match con la spec del ticket. Un type definition separato sarebbe over-engineering per Step 1.

## References

* AGENTS.md v0.1 §"Feature Freeze — REVOCATO" (2026-07-06) + §"Regole di lavoro" + §"Anti-duplication Rules" + §"Piano operativo canonico" (single-identity + no new singleton/registry/resolver/cache/service-locator + 4 commit atomic uno per step su main).
* AGENTS.md §"Install Pipeline Plumbing" — `tools/wrap_push.sh` (GATE-MNT-01 push-side wrapper usato per ogni push di M1.7) + `tools/check_doc_sync.sh` (gate #7 — 4 doc canonical files all updated in each commit).
* [ADR-005 — asset-resolver-local](./ADR-005-asset-resolver-local.md) (the engine-local AssetResolver che `chronon3d::AssetRegistry` di `asset_registry.hpp` canonizza — la base che `AssetPreflightResolver` astrae).
* [ADR-011 — camera-legacy-deletion](./ADR-011-camera-legacy-deletion.md) (la canonicalisation close-out precedent per camera_v1 — stessa filosofia, dominio camera. Phase 1 (this ADR-style) = design; Phase 2 = call-site migration; Phase 3 = declaration deletion; Phase 4 = boundary-gate promotion. M1.7 segue lo stesso pattern a 4 fasi ma con i 2 workstream paralleli Sequence + Asset).
* [ADR-013 — camera_v1-semantics](./ADR-013-camera-v1-semantics.md) (Decision 5 "default-then-promotion" pattern, the architectural precedent per "build explicit invariants in the producer, document them, lock with tests" — ripreso da ADR-015 Decision 1 e da questo ADR Decision 2).
* [ADR-015 — screen-space-trs-invariant](./ADR-015-screen-space-trs-invariant.md) (the producer-owns-invariant pattern ripreso da Decision 2 di questo ADR: `AssetPreflightResolver` scrive `ok = missing.empty()` invece di lasciare al consumer il compito di controllare; stesso pattern di `project_layer_2_5d()` che scrive `transform.scale.z = 1.0f` invece di lasciare al consumer il compito di normalizzare).
* [`docs/tickets/TICKET-SEQUENCE-LOCAL-FRAME.md`](../tickets/TICKET-SEQUENCE-LOCAL-FRAME.md) — 5 legacy items Sequence A-E + 4-step migration plan + §Grep-Audit Pre-Step-4 Snapshot (254 hits).
* [`docs/tickets/TICKET-ASSET-READINESS.md`](../tickets/TICKET-ASSET-READINESS.md) — 5 legacy items Asset A-E + 4-step migration plan + §Grep-Audit Pre-Step-4 Snapshot (85 hits).
* `docs/FOLLOWUP_TICKETS.md` §M1.7 P1 backlog rows — 2 ticket `PARTIAL (Step 1/4 DONE)` (TICKET-SEQUENCE-LOCAL-FRAME Step 1+2 done, TICKET-ASSET-READINESS Step 1 done).
* `docs/ROADMAP.md` §M1.7 milestone — `Sequence + Asset Readiness (Single Source of Truth)` con le 2 ticket + 4-step plan.
* `docs/CURRENT_STATUS.md` §M1.7 paragraph bumped — `Sequence Step 1/4 + Step 2/4 DONE` + `Asset Step 1/4 DONE` con callout verbatim.
* `docs/CHANGELOG.md` 2 entries (Luglio 2026) — Step 1+2 Sequence + Step 1 Asset.
* `include/chronon3d/timeline/timeline_resolver_v2.hpp` — Step 1 Sequence: 4 simboli canonici landed (Step 1 commit `main@0f47d591`).
* `include/chronon3d/timeline/legacy_adapters.hpp` — Step 2 Sequence: 3 adapters landed (Step 2 commit `main@fab2046e`).
* `include/chronon3d/assets/asset_readiness_v2.hpp` — Step 1 Asset: 5 simboli canonici landed (Step 1 commit `main@33798b0a`).
* `include/chronon3d/timeline/sequence.hpp` — `chronon3d::SequenceContext` esistente (coesiste con `chronon3d::timeline::v2::SequenceNode` namespace-disjoint).
* `include/chronon3d/assets/asset_metadata.hpp` — `chronon3d::AssetType` esistente (coesiste con `chronon3d::assets::v2::AssetKind` namespace-disjoint, valori intenzionalmente non-allineati).
* `include/chronon3d/assets/asset_registry.hpp` — `chronon3d::AssetRegistry` esistente (coesiste con `chronon3d::assets::v2::AssetManifest` namespace-disjoint, semantica complementare).
* `tools/check_legacy_timeline_prevalence.sh` — forward-only grep-gate tool per 5 Sequence legacy items A-E.
* `tools/check_legacy_asset_prevalence.sh` — forward-only grep-gate tool per 5 Asset legacy items A-E.
* `tools/wrap_push.sh` — GATE-MNT-01 push-side wrapper usato per ogni push di M1.7.
* `tools/check_doc_sync.sh` — gate #7 — 4 doc canonical files all updated in each commit (CHANGELOG + CURRENT_STATUS + FOLLOWUP_TICKETS + 1 ticket).
* `tools/check_architecture_boundaries.sh` — deve restare 16/16 PASS dopo Step 4 (zero public API surface regression).
* `docs/CHANGELOG.md` — TICKET-P1-07 + TICKET-P1-11 referenced come "lavoro prerequisito aggregato" (asset resolver globale + timeline percorsi multipli). M1.7 = concretizzazione canonicale di entrambi.
