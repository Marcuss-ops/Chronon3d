# TICKET-P1-ACTION-PLAN — Matrice di avanzamento P1 (2026-07-04)

> Feature Freeze V0.1 attivo (2026-06-29). Solo fix di build, test deterministici,
> rimozione legacy path, consumer SDK, e allineamento documentazione sono consentiti.
> Stato riflesso in [`docs/CURRENT_STATUS.md`](../CURRENT_STATUS.md) e [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md).
>
> **M1.5#15 (2026-07-04):** riformulato come matrice osservabile. Ogni cella
> riflette lo stato del codice osservabile su `main` (post commit
> `5320eb29` = TICKET-RUNTIME-ADAPTER-INCOMPLETE-TYPE fix + `6491cdff` = M1.5#10
> (1/4) + `2b484d81` = M1.5#14 housekeeping). La versione precedente era
> pessimistica in alcuni punti (es. Multi-run failure Test era "Missing" ma il
> codice mostra 3 test file dedicati — `test_text_run_multi_run_failure_policy`,
> `test_compile_text_layout_per_paragraph_failure`,
> `test_compile_text_layout_errors`).

## Matrice di avanzamento

| Tema                          | Implementazione | Test      | Migrazione | Rimozione legacy |
| ----------------------------- | --------------- | --------- | ---------- | ---------------- |
| Multi-run failure (`P1 #1`)   | **Done**        | **Done**  | **Done**   | N/A              |
| Determinismo bidi (`P1 #2`)   | **Done**        | **Done**  | **Done**   | N/A              |
| Session cache (`P1 #3`)       | **Done**        | **Missing** | **Partial** | **Missing**    |
| Legacy pipeline (`P1 #4`)     | **Missing**     | Gate only | **Partial** | **Missing**    |
| CMake boundary (`P1 #5`)      | **Done**        | **Done**  | **Done**   | **Done**         |

### Legenda

| Valore     | Significato                                                                  |
| ---------- | ---------------------------------------------------------------------------- |
| Done       | Verificato su commit osservabile (file + line ref nel dettaglio)             |
| Partial    | Lavoro iniziato; commit osservabile presente ma copertura incompleta         |
| Missing    | Non ancora iniziato; nessun commit osservabile nella working tree            |
| N/A        | Non applicabile a questo tema (es. nuove feature senza codice legacy)         |
| Gate only  | Verificato solo tramite architecture gate (nessun test funzionale dedicato)   |

> "Osservabile" = il valore della cella è supportato da almeno un file/line/commit
> referenziato nella sezione `## Dettaglio per tema` sottostante. Se una cella
> dice "Done" senza evidenza nel dettaglio, è un false positive da correggere.

---

## Dettaglio per tema

### 1. Multi-run failure (`P1 #1`)

**File canonici:** `src/text/compiler/text_run_shaping.cpp` (stage `apply_failure_policy`),
`src/text/text_run_builder.cpp` (orchestrator `compile_text_layout`),
`src/text/compiler/text_compile_internal.hpp` (stage signature).

| Dimensione        | Stato        | Evidenza osservabile |
| ----------------- | ------------ | -------------------- |
| Implementazione   | **Done**     | `apply_failure_policy` in `text_run_shaping.cpp:184` con switch su `ShapingFailurePolicy::FailWholeParagraph` (default). Orchestrator `compile_text_layout` in `text_run_builder.cpp:230` chiama `tci::apply_failure_policy` (text_run_builder.cpp:292). `ShapingFailurePolicy` enum in `include/chronon3d/text/text_run_builder.hpp:217` con 4 valori: `FailWholeParagraph` / `FallbackFont` / `ReplacementGlyph` / `PlaceholderDiagnostic`. |
| Test              | **Done**     | 3 test file dedicati: `tests/text/test_text_run_multi_run_failure_policy.cpp` (3 TEST_CASE: 3-paragraph accumulator, middle multi-font Err, consecutive `\n`), `tests/text/test_compile_text_layout_per_paragraph_failure.cpp` (per-paragraph Err preservation), `tests/text/test_compile_text_layout_errors.cpp` (compile_text_layout direct-call failure paths). |
| Migrazione        | **Done**     | Da silent-failure (vector skip) a esplicita `Result<PlacedGlyphRun, TextRunError>` con policy esplicita. |
| Rimozione legacy  | **N/A**      | Nuova feature, nessun codice legacy da rimuovere. |

**Feature Freeze:** Implementazione + test pre-freeze. M1.5#5 (commit `7e9e17d3`)
ha completato la migrazione; M1.5#6 (commit `6cc290e5`) ha solidificato la
pipeline a 7 stage. Categoria 2 (test deterministici) — lock Cat-2.

---

### 2. Determinismo bidi (`P1 #2`)

**File canonici:** `src/text/resolver/text_bidi_resolver.cpp` (FriBidi service),
`src/backends/text/bidi_segmenter.cpp` (UAX #9 implementation),
`include/chronon3d/text/text_resolver.hpp` (canonical service).

| Dimensione        | Stato        | Evidenza osservabile |
| ----------------- | ------------ | -------------------- |
| Implementazione   | **Done**     | `text_bidi_resolver.cpp` (M1.5#8 split) + `text_bidi_resolver.hpp:40` (Unicode Bidirectional Algorithm UAX #9 segmenter). `bidi_segmenter.cpp:33-43` definisce `BidiRun.direction` + FriBidi integration. Service canonico: `FontResolver::resolve(...)` (unico, internal-only). |
| Test              | **Done**     | `tests/text/test_text_font_resolver_golden.cpp` (M1.5#8 new) con FNV-1a hash su `ResolvedTextTree`; `tests/text/test_text_bidi.cpp` (6+ TEST_CASE su LTR/RTL/auto direction). Env-var `CHRONON3D_FORCE_NO_FRIBIDI` (introdotto M1.5#8 in `bidi_segmenter.cpp`) — read-only `getenv` per golden test determinismo cross-FriBidi-system. |
| Migrazione        | **Done**     | FriBidi è il path canonico (no fallback a system fonts). `getenv` override solo per golden test cross-system determinism. Font fallback ora asset-resolved via `FontResolver::resolve(...)` con memoization, no più famiglie di sistema. |
| Rimozione legacy  | **N/A**      | Problema di configurazione/feature, non di codice legacy. |

**Feature Freeze:** Cat-2 (test deterministici) — M1.5#8 (commit `pending
this session`) ha introdotto `CHRONON3D_FORCE_NO_FRIBIDI` env-var lock.

---

### 3. Session cache (`P1 #3`)

**File canonici:** `include/chronon3d/backends/software/software_session_resources.hpp`
(canonical ownership), `src/backends/text/text_rasterizer_render.cpp` (legacy static),
`src/backends/text/glyph_atlas.cpp` (legacy static).

| Dimensione        | Stato          | Evidenza osservabile |
| ----------------- | -------------- | -------------------- |
| Implementazione   | **Done**       | `SoftwareSessionResources.text_resources` (M1.5#7 commit `2db652fb`) — canonical ownership path. `RenderSession::layout_cache` (P1 #3). `TextLayoutCache*` esplicito nelle driver function (Fase B2+B3). `process_wide_*` rimossi (Fase B2 commit `7058dacc`). `shared_text_layout_cache()` / `reset_shared_text_layout_cache()` rimossi (Fase B3 commit `876a14ac`). |
| Test              | **Missing**    | Nessun test di isolamento tra due `RenderSession` parallele che non condividono cache. Grep `test_render_session_isolation` / `test_parallel_session` → 0 hit. |
| Migrazione        | **Partial**    | `TextRenderResources` integration landed (M1.5#7 — canonical ownership). Ma `Blend2DResources` static → member migration **NON** completata; `GlyphAtlas` static → member migration **NON** completata. Tracked in [`TICKET-M1.5#7-FULL-SPLIT.md`](TICKET-M1.5#7-FULL-SPLIT.md). |
| Rimozione legacy  | **Missing**    | `static Blend2DResources` ancora in `text_rasterizer_render.cpp`. `static GlyphAtlas` ancora in `glyph_atlas.cpp`. `static std::call_once` patterns rimossi (TICKET-077b done), ma i singleton-static resources no. |

**Feature Freeze:** Implementazione done. Test + Migrazione + Rimozione legacy
bloccate da feature freeze (post-baseline). `M1.5#7-FULL-SPLIT` è la chiusura
canonica.

---

### 4. Legacy pipeline (`P1 #4`)

**File canonici:** `src/backends/software/processors/text/software_text_processor.cpp`
(legacy processor), `src/backends/text/text_rasterizer_render.cpp` (legacy rasterizer),
`include/chronon3d/text/rich_text.hpp` (legacy polyfill),  <!-- drift-allow: stale-ref -->
`tools/check_legacy_text_pipeline.sh` (architecture gate).

| Dimensione        | Stato        | Evidenza osservabile |
| ----------------- | ------------ | -------------------- |
| Implementazione   | **Missing**  | `rasterize_text_to_bl_image` ancora definito in `text_rasterizer_render.cpp:360`. `TextLayoutEngine::layout` ancora definito in `text_layout_engine.hpp:828`. Legacy processor `SoftwareTextProcessor` ancora in `software_text_processor.cpp:118` (chiama `rasterize_text_to_bl_image`). |
| Test              | **Gate only** | `tools/check_legacy_text_pipeline.sh` (gate `check_legacy_text_pipeline`) — 3/3 check pass post-`2b484d81`: r2bi + tle + census cross-verify. Nessun test funzionale di rimozione (es. "after-remove: code non compila se reintroduci"). |
| Migrazione        | **Partial**  | **2 callsite da migrare** (5-step sequence plan in `TICKET-M1.5#9-SEQUENCE.md` + `TICKET-M1.5#10-SEQUENCE.md`): (a) `software_text_processor.cpp:118` → `draw_text_run` (M1.5#9 1/5 done — `install_consumer/main.cpp` migrated; M1.5#9 2-5 PLANNED); (b) `rich_text.hpp:257` → `TextDocument` (M1.5#10 1/4 done — test_design_kit.cpp obsolete TEST_CASE dropped; M1.5#10 2-4 PLANNED). |
| Rimozione legacy  | **Missing**  | `SoftwareTextProcessor` + `text_rasterizer_render.cpp` + `rich_text.hpp` ancora presenti. Tree `src/backends/software/processors/text/` (7 file) + `src/backends/text/text_rasterizer_*.cpp` (3 file) + `rich_text.hpp` (380 LOC) da cancellare. |

**Feature Freeze:** Cat-3 (rimozione legacy path) — consentito. M1.5#9 (1/5)
+ M1.5#10 (1/4) done; rimozione effettiva completa richiede M1.5#9 (2-5) +
M1.5#10 (2-4) + baseline-verde.

**Censimento callsite legacy (gate-verified, no drift):**

`rasterize_text_to_bl_image`:

| # | File                                       | Classificazione |
|---|--------------------------------------------|-----------------|
| 1 | `src/backends/text/text_rasterizer_render.cpp` | **DEFINITION** (line 360) |
| 2 | `src/backends/text/text_rasterizer_ink.cpp`    | Internal helper (extracted) |
| 3 | `src/backends/software/processors/text/software_text_processor.cpp` | **DEPRECATE** (line 118) |
| 4 | `apps/chronon3d_cli/commands/dev/text_audit_engine.cpp` | CLI diagnostic |
| 5 | `tests/text/test_text_material.cpp`             | Test |
| 6 | `include/chronon3d/backends/text/text_rasterizer_utils.hpp` | Declaration |

`TextLayoutEngine::layout`:

| # | File                                       | Classificazione |
|---|--------------------------------------------|-----------------|
| 1 | `include/chronon3d/backends/text/text_layout_engine.hpp` | **DEFINITION** |
| 2 | `src/backends/text/text_rasterizer_render.cpp` | **DEPRECATE** |
| 3 | `include/chronon3d/text/rich_text.hpp`        | **DEPRECATE** (line 257) |  <!-- drift-allow: stale-ref -->
| 4 | `apps/chronon3d_cli/commands/dev/text_audit_engine.cpp` | CLI diagnostic |
| 5-9 | `tests/text/test_text_*.cpp` (5 file)        | Test |

---

### 5. CMake boundary (`P1 #5`)

**File canonici:** `src/CMakeLists.txt`, `cmake/Chronon3DSdkArchive.cmake` (post-P1 #12).

| Dimensione        | Stato        | Evidenza osservabile |
| ----------------- | ------------ | -------------------- |
| Implementazione   | **Done**     | `target_link_libraries(chronon3d_core INTERFACE chronon3d_backend_video)` rimosso. Contratto CMake allineato (video backend è INTERFACE di `chronon3d_software`, non di `chronon3d_core`). |
| Test              | **Done**     | `chronon3d_software` linka già `chronon3d_backend_video` (via `target_link_libraries` esplicito). `chronon3d_pipeline` linka `chronon3d_core` + `chronon3d_software`. Nessun consumer perde l'accesso al backend video. Compilazione clean confermata da `tools/check_architecture_boundaries.sh` (gate #1). |
| Migrazione        | **Done**     | Fix applicato direttamente su `main`, nessuna migrazione necessaria. |
| Rimozione legacy  | **Done**     | Link contraddittorio rimosso; nessun `target_link_libraries` INTERFACE residuo su `chronon3d_core` verso backend specifici. |

**Feature Freeze:** ✅ Completato (fix di build, categoria 1). Pre-M1.5#1.

---

## Ordine di esecuzione (post-baseline)

| Step | Tema               | Azione                                                       | Categoria Freeze |
|------|--------------------|--------------------------------------------------------------|------------------|
| 1    | CMake boundary     | ✅ Fatto (`Done` × 4)                                        | Consentito       |
| 2    | Legacy pipeline    | Censimento callsite + architecture gate (gate-only)           | ⚠️ Consentito   |
| 3    | Determinismo bidi  | Test deterministico multi-piattaforma (`Done` post-M1.5#8)   | ⚠️ Consentito   |
| 4    | Multi-run failure  | Per-run Result + policy esplicita (`Done` post-M1.5#5+#6)    | ❌ Post-baseline |
| 5    | Legacy pipeline    | M1.5#9 2-5: migrate `RenderNodeFactory::text()` + drop registration + delete tree + delete rasterizer | ❌ Post-baseline |
| 6    | Legacy pipeline    | M1.5#10 2-4: sweep `RichTextRun` + sweep `draw_rich_text` + DELETE `rich_text.hpp` + `rich_text.cpp` | ❌ Post-baseline |
| 7    | Session cache      | Test isolamento tra sessioni parallele                       | ❌ Post-baseline |
| 8    | Session cache      | M1.5#7-FULL-SPLIT: `Blend2DResources` + `GlyphAtlas` static → member | ❌ Post-baseline |
| 9    | Determinismo bidi  | Manifest render include font_hash + shaping_lib_version      | ❌ Post-baseline |

> Step 5 e 6 (Legacy pipeline removal) sono il **blocco bloccante** per la
> revoca del feature freeze: senza rimozione completa, il gate
> `check_legacy_text_pipeline` continua a richiedere la whitelist e
> l'ABI non si contrae. Companion: [`TICKET-M1.5#9-SEQUENCE.md`](TICKET-M1.5#9-SEQUENCE.md)
> + [`TICKET-M1.5#10-SEQUENCE.md`](TICKET-M1.5#10-SEQUENCE.md).
