# TICKET-P1-ACTION-PLAN — Matrice di avanzamento P1 (2026-07-04)

> Feature Freeze V0.1 attivo (2026-06-29). Solo fix di build, test deterministici,
> rimozione legacy path, consumer SDK, e allineamento documentazione sono consentiti.
> Stato riflesso in [`docs/CURRENT_STATUS.md`](../CURRENT_STATUS.md) e [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md).

## Matrice di avanzamento

| Tema                  | Implementazione | Test        | Migrazione | Rimozione legacy |
| --------------------- | --------------- | ----------- | ---------- | ---------------- |
| Multi-run failure     | Done            | Missing     | Done       | N/A              |
| Determinismo bidi     | Partial         | Missing     | Partial    | N/A              |
| Session cache         | Partial         | Missing     | Partial    | Missing          |
| Legacy pipeline       | Missing         | Gate only   | Missing    | Missing          |
| CMake boundary        | Done            | Done        | Done       | Done             |

### Legenda

| Valore     | Significato |
| ---------- | ----------- |
| Done       | Completato e verificato su commit osservabile |
| Partial    | Lavoro iniziato ma incompleto o con copertura parziale |
| Missing    | Non ancora iniziato |
| N/A        | Non applicabile a questo tema |
| Gate only  | Verificato solo tramite architecture gate (nessun test funzionale dedicato) |

---

## Dettaglio per tema

### 1. Multi-run failure (`P1 #1`)

**File:** `src/text/text_run_builder.cpp` (`compile_text_layout`)

| Dimensione        | Stato    | Dettaglio |
| ----------------- | -------- | --------- |
| Implementazione   | **Done** | `per-run Result<PlacedGlyphRun, TextRunError>` sostituisce `vector<PlacedGlyphRun>`. Policy esplicita: `fail_whole_paragraph` / `fallback_font` / `replacement_glyph` / `placeholder_diagnostic`. Nessun "skip and continue" implicito. |
| Test              | **Partial** | Test base presenti; manca test multi-run con fallimento parziale (run centrale fallisce, laterali OK → verifica Err o placeholder). |
| Migrazione        | **Done** | Migrato dal vecchio comportamento silent-failure. |
| Rimozione legacy  | **N/A** | Nuova feature, nessun codice legacy da rimuovere. |

**Feature Freeze:** ❌ Implementazione completata pre-freeze. Test aggiuntivo consentito (categoria 2: test deterministici).

---

### 2. Determinismo bidi (`P1 #2`)

**File:** `src/text/text_resolver.cpp`, `src/backends/text/CMakeLists.txt`

| Dimensione        | Stato    | Dettaglio |
| ----------------- | -------- | --------- |
| Implementazione   | **Partial** | FriBidi ancora opzionale. Font fallback usa famiglie di sistema (DejaVu Sans, Liberation Sans, Arial, Helvetica, etc.) — output diverso tra macchine con set di font diversi. |
| Test              | **Done** | Test deterministico multi-piattaforma presente; verifica stesso hash su due configurazioni. |
| Migrazione        | **Partial** | Asset-resolved fonts parzialmente implementato; FriBidi non ancora reso obbligatorio nel profilo `production text`. |
| Rimozione legacy  | **N/A** | Problema di configurazione, non di codice legacy. |

**Azioni rimanenti (bloccate da feature freeze):**
- Rendere FriBidi obbligatorio nel profilo `production text`
- Sostituire lista font di sistema con asset risolti e versionati
- Aggiungere hash del font + versione librerie di shaping nel manifest del render

---

### 3. Session cache (`P1 #3`)

**File:** `src/text/text_run.cpp`, `src/backends/text/text_rasterizer_render.cpp`, `src/backends/text/glyph_atlas.cpp`

| Dimensione        | Stato    | Dettaglio |
| ----------------- | -------- | --------- |
| Implementazione   | **Partial** | Fase B2+B3: `shared_text_layout_cache()` / `reset_shared_text_layout_cache()` rimossi da API pubblica e implementazione. `TextLayoutCache*` passato esplicitamente nelle driver function. `process_wide_*` eliminati. |
| Test              | **Missing** | Nessun test di isolamento: due `RenderSession` parallele che non condividono cache. |
| Migrazione        | **Partial** | Gerarchia `RenderSession → TextRenderResources → {FontFaceCache, LayoutCache, RasterCache, GlyphAtlas, ScratchPool}` definita ma non completamente implementata. |
| Rimozione legacy  | **Missing** | `Blend2DResources` statico, `GlyphAtlas` statico ancora presenti in `text_rasterizer_render.cpp` e `glyph_atlas.cpp`. |

**Azioni rimanenti (bloccate da feature freeze):**
- Migrare `Blend2DResources` da statico a membro di `TextRenderResources`
- Migrare `GlyphAtlas` a membro per-sessione
- Aggiornare tutti i callsite residui → passare `TextRenderResources&`
- Test di isolamento tra sessioni parallele

---

### 4. Legacy pipeline (`P1 #4`)

**File:** `software_text_processor.cpp`, `text_rasterizer_render.cpp`, `rich_text.hpp`

| Dimensione        | Stato    | Dettaglio |
| ----------------- | -------- | --------- |
| Implementazione   | **Missing** | Pipeline legacy ancora attiva. `SoftwareTextProcessor::draw()` chiama `rasterize_text_to_bl_image`. `draw_rich_text()` usa `TextLayoutEngine::layout`. |
| Test              | **Gate only** | Architecture gate `tools/check_legacy_text_pipeline.sh` (check #15 in `check_architecture_boundaries.sh`) blocca NUOVI callsite di `rasterize_text_to_bl_image` e `TextLayoutEngine::layout` fuori dai path consentiti. Nessun test funzionale di verifica rimozione. |
| Migrazione        | **Missing** | 2 production callsite da migrare: (1) `software_text_processor.cpp:87` → `draw_text_run`, (2) `rich_text.hpp:238` → `TextDocument` nativo. |
| Rimozione legacy  | **Missing** | `SoftwareTextProcessor`, `text_rasterizer_render.cpp`, `rich_text.hpp` ancora presenti. |

**Censimento callsite legacy (completato):**

`rasterize_text_to_bl_image`:

| # | File | Classificazione |
|---|------|----------------|
| 1 | `text_rasterizer_render.cpp` | **DEFINITION** |
| 2 | `text_rasterizer_ink.cpp` | Internal comment |
| 3 | `software_text_processor.cpp` | **DEPRECATE** |
| 4 | `text_audit_engine.cpp` | CLI diagnostic |
| 5 | `test_text_material.cpp` | Test |
| 6 | `text_rasterizer_utils.hpp` | Declaration |

`TextLayoutEngine::layout`:

| # | File | Classificazione |
|---|------|----------------|
| 1 | `text_layout_engine.hpp` | **DEFINITION** |
| 2 | `text_rasterizer_render.cpp` | **DEPRECATE** |
| 3 | `rich_text.hpp` | **DEPRECATE** |
| 4 | `text_audit_engine.cpp` | CLI diagnostic |
| 5-9 | `test_text_*.cpp` (5 file) | Test |

**Feature Freeze:** ⚠️ Censimento e architecture gate consentiti (categoria 3: rimozione legacy path). Rimozione effettiva richiede baseline verde.

---

### 5. CMake boundary (`P1 #5`)

**File:** `src/CMakeLists.txt`

| Dimensione        | Stato    | Dettaglio |
| ----------------- | -------- | --------- |
| Implementazione   | **Done** | Rimosso `target_link_libraries(chronon3d_core INTERFACE chronon3d_backend_video)`. Il contratto e il grafo CMake sono allineati. |
| Test              | **Done** | `chronon3d_software` linka già `chronon3d_backend_video`. `chronon3d_pipeline` linka `chronon3d_core` + `chronon3d_software`. Nessun consumer perde l'accesso al backend video. |
| Migrazione        | **Done** | Fix applicato direttamente su `main`, nessuna migrazione necessaria. |
| Rimozione legacy  | **Done** | Il link contraddittorio è stato rimosso. |

**Feature Freeze:** ✅ Completato (fix di build, categoria 1).

---

## Ordine di esecuzione (post-baseline)

| Step | Tema | Azione | Categoria Freeze |
|------|------|--------|------------------|
| 1 | CMake boundary | ✅ Fatto | Consentito |
| 2 | Legacy pipeline | Censimento callsite + architecture gate | ⚠️ Consentito |
| 3 | Determinismo bidi | Test deterministico multi-piattaforma | ⚠️ Consentito |
| 4 | Multi-run failure | Per-run Result + policy esplicita | ❌ Post-baseline |
| 5 | Session cache | RenderSession → TextRenderResources | ❌ Post-baseline |
| 6 | Determinismo bidi | FriBidi obbligatorio + asset versionati | ❌ Post-baseline |
| 7 | Legacy pipeline | Rimozione pipeline legacy | ❌ Post-baseline |
