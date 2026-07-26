# ADR-017 — Render preparation orchestrator: prepare_render(...) composes manifest + preflight + decode + warmup reusing existing services; never a second AssetResolver or a parallel cache

| Field | Value |
|---|---|
| **Status** | 📋 Documented (Step 1/2 introduzione additive — atomic commit `docs(adr): ADR-017 render-preparation-orchestrator`; Step 2 migration in `render still`/`render video`/`chunked export`/`pipe export`/`CLI render` forward-pointed) |
| **Date** | 2026-07-26 |
| **Deciders** | M1.7 + Fase 5 RenderPreparation orchestrator workstream (chronon3d Asset Readiness single-source-of-truth lineage + prepare_render pipeline integration) |
| **Tags** | `render-preparation`, `orchestrator`, `preflight`, `warmup`, `single-source-of-truth`, `cat-3-anti-dup`, `asset-readiness`, `M1.7-followup`, `Fase-5` |
| **Related** | [ADR-005 — asset-resolver-local](./ADR-005-asset-resolver-local.md) (engine-local AssetResolver che `chronon3d::AssetRegistry` canonizza); [ADR-016 — sequence + asset canonical contract](./ADR-016-sequence-asset-canonical-contract.md) (Decision 2: `chronon3d::assets::v2::AssetPreflightResolver` già landed, Decision 3: 3 regole finali di ownership); `docs/tickets/TICKET-ASSET-READINESS.md` (5 legacy items Asset A-E + 4-step safe migration); `include/chronon3d/runtime/warmup_renderer.hpp` (existing warmup infra da riusare); `include/chronon3d/assets/asset_readiness_v2.hpp` (AssetPreflightResolver esistente); AGENTS.md §"Anti-duplication Rules" (NO nuovi singleton/registry/cache/service-locator); AGENTS.md §"Regole di lavoro" (PR piccole e mirate); AGENTS.md §"Fix piccolo NON aggiornare i canonici" (test-only commit skip canonici); `tools/wrap_push.sh` (GATE-MNT-01 push-side wrapper); `tools/check_doc_sync.sh` (gate #7 — 4 doc canonical files all updated in each commit). |

## Context

### Problema attuale

Chronon3D ha già investito molto sulla canonicalizzazione asset readiness (`chronon3d::assets::v2::{AssetKind, AssetRef, AssetManifest, AssetPreflightResult, AssetPreflightResolver}` landed commit `33798b0a` per ADR-016 Decision 2). Esiste inoltre `runtime::warmup_renderer(renderer, composition, warmup_options)` che prealloca framebuffer, tocca memoria e scalda pool/cache con 2 dummy frame. Tuttavia la composizione "verifica che gli asset sono pronti + decodifica effettiva + warmup del renderer" prima di un render loop è **frammentata in 5 punti di decisione** (mirror del pattern TICKET-ASSET-READINESS §Violation 2):

1. **Comando `render still`**: sequenza preflight + warmup + render ad hoc nel command file.
2. **Comando `render video`**: sequenza leggermente diversa, gestisce atomic output via `.partial` rename + ffprobe validation.
3. **Comando `chunked export`**: sequenza con chunk-local warmup (warmup ad ogni chunk — spreco).
4. **Comando `pipe export`**: pipe-output path con sequence propria.
5. **Comando `CLI render` (legacy)**: sequenza legacy con warmup differito.

Conseguenze:

- **Drift tra command**: bug fix al preflight di `render still` non si propaga a `render video` (causa: nessun single chokepoint).
- **Warmup ridondante**: `chunked export` chiama `warmup_renderer` ad ogni chunk — N× il costo di warmup su N chunks.
- **Asset discoverability render-time**: alcuni command eseguono `image_cache.decode()` dentro il render loop, generando latenza per-frame.
- **Cat-3 anti-dup risk**: 5 sequenze simili ma divergenti sono terreno fertile per drift e bug-fixes locali che non propagano.
- **Nessun test unico**: non esiste un `tests/runtime/test_render_preparation.cpp` che verifica l'idempotenza del barrier pre-render.

### Filosofia architetturale violata

Questa duplicazione viola la regola **AGENTS.md v0.1 §"Anti-duplication Rules"**:

> *No duplicare registry, resolver, sampler, cache, service locator o checklist.*

Più specificamente, le 5 sequenze divergenti sono figlie di **due regole finali non scritte ma implicite** che questo ADR codifica:

1. **`prepare_render(...)` è l'unico orchestratore** per le 7 fasi (manifest → preflight → font/image/video/audio decode → warmup). Non è un secondo AssetResolver né sostituisce `AssetPreflightResolver` né sostituisce `warmup_renderer`. È una composizione thin sopra i servizi canonici esistenti.
2. **`RenderPreparationResult` è POD per-value return** — non singleton, non globale, non thread-local. NON possiede cache proprie; riusa `image_cache`, `font_engine`, `runtime::warmup_renderer` esistenti.

Queste due regole sono il **contratto canonico** che questo ADR formalizza come design decision.

## Decision

Questo ADR codifica 1 decisione di design + 3 regole finali vincolanti + 1 vincolo architetturale + 1 regola anti-blocking, formando il **contratto SSoT per render preparation** del Chronon3D pipeline.

### Decision 1 — RenderPreparation contract: thin wrapper che compone 7 fasi in ordine

Il **contratto RenderPreparation single-source-of-truth** è:

| Symbol | Type | Role |
|---|---|---|
| `RenderPreparationOptions` | `struct { PreflightMode preflight_mode{PreflightMode::FullComposition}; bool prepare_fonts{true}; bool prepare_images{true}; bool prepare_video_metadata{true}; bool prepare_audio_metadata{true}; bool warmup_renderer{true}; Frame reference_frame{0}; }` | POD opzioni per il barrier pre-render. Default = FullComposition + tutte le 4 prepare_* + warmup attivo. |
| `RenderPreparationResult` | `struct { std::vector<PreflightIssue> issues; std::size_t fonts_prepared{0}; std::size_t images_prepared{0}; std::size_t videos_prepared{0}; std::size_t audio_prepared{0}; RendererWarmupResult warmup{}; [[nodiscard]] bool ok() const { return !has_preflight_errors(issues); } }` | POD per-value return. `ok()` = gate failure-fast prima dell'encoder. |
| `prepare_render` | `[[nodiscard]] RenderPreparationResult prepare_render(SoftwareRenderer& renderer, const Composition& composition, const RenderPreparationOptions& options = {});` | Funzione canonica. 7-fasi thin wrapper. NON member di struct (no singleton/registry). Header `include/chronon3d/runtime/render_preparation.hpp` (new). |

**Pipeline interna a 7 fasi** (ordine vincolante):

```
1. AssetManifest build dalla Composition
   ↓
2. chronon3d::assets::v2::AssetPreflightResolver::preflight(manifest)
   ↓ (se ok() == false, ritorna failed_result con missing[] espliciti)
3. Per ogni Font asset: font_engine.load_font(*resolved_path)
   ↓
4. Per ogni Image asset: image_cache.decode_and_store(*resolved_path)
   ↓
5. Per ogni Video asset: video_decoder.read_metadata(*resolved_path)
   ↓
6. Per ogni Audio asset: audio_decoder.read_metadata(*resolved_path)
   ↓
7. runtime::warmup_renderer(renderer, composition, warmup_options)
   ↓
return RenderPreparationResult{ issues=[], fonts_prepared, images_prepared, videos_prepared, audio_prepared, warmup=... }
```

**Namespace**: top-level `chronon3d::` (coerente col pattern header-only POD). Forward-point: namespace `chronon3d::runtime::v0` è un'alternativa considerata in §Alternatives B, ma il top-level `chronon3d::` riduce il namespace-import noise ai call site.

### Decision 2 — 3 regole finali (riformulate da ADR-016 Decision 3)

1. **`prepare_render(...)` è l'unico orchestratore** per le 7 fasi. Non sostituisce `AssetPreflightResolver` (che rimane la SSoT per "asset pronti?" via `chronon3d::assets::v2::preflight(manifest)`); non sostituisce `warmup_renderer` (che rimane la SSoT per warmup runtime). È una composizione thin.
2. **`RenderPreparationResult` è POD per-value return** — NON singleton, NON globale, NON thread-local. `has_preflight_errors(issues)` è il gate che decide se l'encoder parte o FAIL esplicito.
3. **`Renderer` non inventa timeline né asset** — PNG scuri vietati in `chronon3d_install_consumer_tests` Phase 4 (acceptance gate). `prepare_render` ritorna `ok() == false` con messaggio per ogni `missing` PRIMA che il render loop parta.

### Decision 3 — Cat-3 anti-dup enforcement: ZERO nuovi singleton/registry/cache/service-locator

Il **vincolo architetturale** (AGENTS.md v0.1 §"Regole di lavoro" + §"Anti-duplication Rules") che questo ADR ribadisce come permanente per il dominio render preparation:

- `prepare_render` è una **free function stateless** (header-only inline, no class wrapping singleton).
- `RenderPreparationOptions` + `RenderPreparationResult` sono **POD struct** per-value.
- `RenderPreparationResult` NON possiede cache proprie. NON introduce `GlobalPreflight`, `PreflightCache`, `RenderPreparationRegistry` o similari (vietati per nome da AGENTS.md §"Anti-duplication Rules").
- Riusa `AssetManifest` (`chronon3d::assets::v2`), `AssetPreflightResolver` (`chronon3d::assets::v2`), `font_engine`, `image_cache`, `video_decoder_*`, `audio_decoder_*`, `runtime::warmup_renderer` — servizi canonici già esistenti.
- Se un command (es. `chunked export`) vuole warmup locale, NON chiama `warmup_renderer` direttamente — chiama `prepare_render(..., RenderPreparationOptions{.warmup_renderer = true})` che internamente delega.

### Decision 4 — Anti-blocking semantics: nessuna busy-wait pre-render

Il pattern `delayRender()` di Remotion (busy-wait "aspetta che l'asset sia pronto") **NON** è ammesso:

- NO `while (!asset_ready) { sleep(...); }` dentro il frame loop.
- NO polling su filesystem durante render.
- La traduzione corretta per Chronon3D è una **barriera sincrona prima del render loop**: `prepare_render(...)` blocca (sincronicamente) finché le 7 fasi non sono completate; poi ritorna `RenderPreparationResult` POD; poi `RenderJob::start()` esegue `ok()` check; poi il render loop parte. NO frame scritto se `ok() == false`.

## Migration path (2-step safe migration — Decision 5)

Per Marcuss-ops "Non eliminare subito tutto fisicamente" (AGENTS.md §"Regole di lavoro" "PR piccole e mirate, senza mescolare refactor indipendenti"), la canonicalizzazione render preparation è eseguita via **2-step safe migration** per ticket:

| Step | Work | Commit | State |
|---|---|---|---|
| **Step 1 (this ADR)** | Author `docs/adr/ADR-017-render-preparation-orchestrator.md` (status=Documented) + introduce additive `prepare_render(...)` free function + `RenderPreparationOptions` + `RenderPreparationResult` POD. Zero migration callsite. Tutti i test preesistenti PASS bit-identical. | this commit (atomic) | ✅ DONE |
| **Step 2 (forward-point)** | Migration callsite: ogni command file (`render_still.cpp`, `render_video.cpp`, `chunked_export.cpp`, `pipe_export.cpp`, `cli_render.cpp`) chiama `prepare_render(...)` all'inizio della sua pipeline. Rimozione chiamate dirette a `AssetPreflightResolver`/`font_engine`/`image_cache`/`warmup_renderer` dai command file (spostate dentro `prepare_render`). `tools/check_no_direct_preflight_in_commands.sh` (NEW forward-only grep-gate) exit 0 = migration completa. | forward-point | 📋 PLANNED |

### Acceptance criteria per Migration DONE (entrambi i 2 step)

- **Step 2 macchina-verifica**: post `main@7eb5c2ba` baseline-verde + 11/11 PASS + `bash tools/check_no_direct_preflight_in_commands.sh` exit 0 + 10 test SUBCASEs in `tests/runtime/test_render_preparation.cpp` (acceptance §Acceptance criteria sotto) PASS bit-identical.
- **Nessun nuovo singleton/registry/resolver/cache/service-locator** (regola permanente AGENTS.md §"Anti-duplication Rules").
- **PNG scuri vietati** in `chronon3d_install_consumer_tests` Phase 4 (asset fallback vietato → preflight FAIL → no PNG scuri).
- **Boundary gate `tools/check_architecture_boundaries.sh` deve restare 16/16 PASS** dopo Step 2 (zero public API surface regression).

## Acceptance criteria (10 test SUBCASEs per `tests/runtime/test_render_preparation.cpp`)

1. **asset missing → no frame written**: se preflight ritorna `ok() == false` con `missing[]`, `prepare_render` ritorna failed_result, encoder NON parte, zero PNG scritti.
2. **font esistente ma corrotto → preparazione fallisce**: `font_engine.load_font(*path)` su file corrotto → `RenderPreparationResult{ok=false, issues=[font corrupted]}`, render loop mai partito.
3. **immagine esistente ma invalida → preparazione fallisce**: `image_cache.decode_and_store(*path)` su PNG invalido → `ok=false`, encoder mai partito.
4. **idempotenza**: chiamare `prepare_render(...)` due volte consecutive produce `RenderPreparationResult` semanticamente identico (issues == []; counts == counts; warmup == warmup).
5. **ordine del manifest non cambia il risultato**: stesso set di asset in `AssetManifest` indipendentemente dall'ordine di inserimento → stesso `RenderPreparationResult`.
6. **FrameOnly non prepara asset non necessari**: `RenderPreparationOptions{preflight_mode=FrameOnly, reference_frame=Frame{500}}` prepara SOLO gli asset del frame 500, non il resto della composizione.
7. **FullComposition prepara tutto**: `preflight_mode=FullComposition` prepara TUTTI gli asset della composizione.
8. **multithread == serial output**: chiamare `prepare_render` da N thread concorrenti (test thread-safety header-only POD) produce resultati semanticamente identici a N chiamate seriali.
9. **warmup usa il percorso produttivo canonico**: `result.warmup` è popolato da `runtime::warmup_renderer` esistente; nessun secondo warmup path.
10. **default options = FullComposition + tutti i prepare_* + warmup**: `prepare_render(renderer, comp)` (no options) è bit-equivalente a `prepare_render(renderer, comp, RenderPreparationOptions{})`.

## Consequences

### Positive

- **Single source-of-truth per render preparation**: `prepare_render` è l'unica entry point per le 7 fasi. `render still`, `render video`, `chunked export`, `pipe export`, `CLI render` migrati a `prepare_render` non possono divergere per definizione. Bug fix al preflight si propaga automaticamente a tutti i command.
- **Cat-3 anti-dup enforcement esplicito**: Decision 3 ribadisce come permanente il vincolo "NO nuovi singleton/registry/cache/service-locator" per il dominio render preparation. `GlobalPreflight`, `PreflightCache`, `RenderPreparationRegistry` vietati per nome.
- **Warmup efficient in chunked export**: Step 2 migration fa sì che `chunked export` chiami `prepare_render` UNA volta prima del loop (warmup cached via `runtime::warmup_renderer`), non ad ogni chunk. Stima: -80% warmup overhead su 5-chunk export.
- **Anti-blocking semantics**: Decision 4 elimina il pattern busy-wait "delayRender" di Remotion (inappropriato per Chronon3D CPU-first headless). Barriera sincrona è il contratto canonico.
- **Failure mode esplicito**: `RenderPreparationResult.ok()` gate prima dell'encoder previene PNG scuri / silent fallback. `has_preflight_errors(issues)` con messaggio per ogni `missing` fornisce diagnosi actionable all'utente.
- **AGENTS.md v0.1 Cat-1/Cat-3/Cat-5 freeze-compliant**: 3 cat-3 declarations additive (1 struct + 1 struct + 1 free function), zero nuovi singleton/registry/cache, ABI pubblico espanso di ~96B per invocation (footprint trascurabile). Test preesistenti bit-identical garantiti (Step 1 additive-only).

### Negative / Migration cost

- **Step 2 migration forward-pointed**: il lavoro di migration callsite (5 command files) è un multi-PR bounded. Stima: ~150-300 righe modificate in `render_still.cpp` + `render_video.cpp` + `chunked_export.cpp` + `pipe_export.cpp` + `cli_render.cpp`.
- **Forward-only grep-gate `tools/check_no_direct_preflight_in_commands.sh`**: tool da creare come parte di Step 2 forward-point. Pre-Elimination snapshot dei 5 call site diretti da quantificare prima del commit di Step 2.
- **Thread-safety test (SUBCASE 8)**: richiede `std::thread` + `std::async` per stress test; potrebbe avere flaky failure su memory ordering issues se l'impl interna non è veramente stateless. Forward-point: implementare POD strict + nessun mutating state.
- **POD vs owned**: `RenderPreparationResult.issues` è `std::vector<PreflightIssue>` (owned). Strictly POD would richiedere `std::span` o out-param. Trade-off: `vector` è POD-friendly per-value return (move semantics) ma non trivially copyable. Decisione accettata: POD-friendly ma non trivially-copyable.

### Neutral

- **`runtime::warmup_renderer` invariato**: la funzione esistente rimane la SSoT per warmup. `prepare_render` la riusa come step 7 della pipeline.
- **`chronon3d::assets::v2::AssetPreflightResolver` invariato**: la classe esistente rimane la SSoT per "asset pronti?". `prepare_render` la riusa come step 2 della pipeline.
- **`AssetManifest` invariato**: il value-type canonico di ADR-016 Decision 2 rimane. `prepare_render` lo costruisce da `Composition` come step 1.

## Alternatives considered

- **A. Mantenere le 5 sequenze divergenti + grep-gate come unica enforcement.** Rifiutato per le stesse ragioni di ADR-016 Alternative A: la duplicazione è figlia di N punti di decisione che producono behavior inconsistente (warmup ridondante, busy-wait preflight, asset discoverability render-time). Il grep-gate è uno strumento di misura, non di fix. Decision 1+2+3 di questo ADR è il **fix**.
- **B. `RenderPreparation` come singleton o service-locator.** Rifiutato per AGENTS.md §"Anti-duplication Rules" + Decision 3 di questo ADR. I singleton sarebbero esattamente il tipo di duplicazione che il design combatte.
- **C. `prepare_render` come class membro di `SoftwareRenderer`** (es. `renderer.prepare(composition, options)`). Considerato per ridurre il numero di args; rifiutato perché (1) prepara Composition, non Renderer — quindi il primo argomento naturale è Composition; (2) Compositional semantics più chiari se free function; (3) evita di inflazionare la `SoftwareRenderer` API surface (Cat-3 minimal-surface).
- **D. `prepare_render` come method membro di `Composition`** (es. `composition.prepare(renderer, options)`). Stessa logica di (C), rifiutato per (1) Composition non sa nulla di SoftwareRenderer (separation of concerns); (2) Composition è value-type immutabile; (3) prepare stateful side-effect non è Composition-responsibility.
- **E. `prepare_render` come `AssetPreflightResolver::orchestrate(...)` member.** Considerato per accoppiamento stretto con preflight; rifiutato perché (1) introduce un membro stateful su `AssetPreflightResolver` (Cat-3 anti-dup violation: `AssetPreflightResolver` deve restare stateless per ADR-016 Decision 4); (2) `prepare_render` non è solo preflight — è preflight + decode + warmup. Accoppiamento errato.
- **F. `RenderPreparationResult` come `std::optional` per esplicitare failure.** Considerato per type-safety; rifiutato per ergonomia — il caller deve accedere `result.issues` per diagnostics, e `std::optional<T>` richiederebbe `result->issues` (less natural) o `.value()` (more verbose). `bool ok()` POD-method è il giusto trade-off.
- **G. Step 1 + Step 2 in un singolo commit.** Rifiutato per AGENTS.md §"Regole di lavoro" "PR piccole e mirate". I 2 commit separati riflettono 2 stati distinti del codice (Step 1: additive-only, zero callsite migration; Step 2: 5 callsite migration).
- **H. NO migration Step 2 (lasciare le 5 sequenze divergenti).** Rifiutato perché vanifica lo scopo del barrier orchestrator. Senza Step 2, `prepare_render` esiste ma non viene chiamato.

## References

- AGENTS.md v0.1 §"Anti-duplication Rules" (regola permanente NO nuovi singleton/registry/cache/service-locator).
- AGENTS.md v0.1 §"Regole di lavoro" (PR piccole e mirate, senza mescolare refactor indipendenti).
- AGENTS.md v0.1 §"Fix piccolo NON aggiornare i canonici" (test-only commit skip canonici).
- AGENTS.md v0.1 §"Install Pipeline Plumbing" (cat-4 ancillary asset documentale).
- [ADR-005 — asset-resolver-local](./ADR-005-asset-resolver-local.md) (the engine-local AssetResolver che `chronon3d::AssetRegistry` canonizza).
- [ADR-016 — sequence + asset canonical contract](./ADR-016-sequence-asset-canonical-contract.md) (Decision 2: `chronon3d::assets::v2::AssetPreflightResolver` già landed; Decision 3: 3 regole finali di ownership; Decision 4: NO nuovi singleton).
- `docs/tickets/TICKET-ASSET-READINESS.md` (5 legacy items Asset A-E + 4-step safe migration plan + Grep-Audit Pre-Step-4 Snapshot).
- `include/chronon3d/runtime/warmup_renderer.hpp` (existing warmup infra da riusare come Step 7).
- `include/chronon3d/assets/asset_readiness_v2.hpp` (AssetPreflightResolver esistente da riusare come Step 2).
- `tools/wrap_push.sh` (GATE-MNT-01 push-side wrapper usato per ogni push di questo chore).
- `tools/check_doc_sync.sh` (gate #7 — 4 doc canonical files all updated in each commit).
- `tools/check_architecture_boundaries.sh` (deve restare 16/16 PASS dopo Step 2).
- `docs/CURRENT_STATUS.md` §Stato generale per area (Fase 5 RenderPreparation row aggiornato post-landing).
- `docs/FOLLOWUP_TICKETS.md` §Open Blockers (riga blocker per Step 2 migration, planned).
- `docs/CHANGELOG.md` (silent one-liner Cita-Only pattern).