# TICKET-P1-ACTION-PLAN — Piano d'azione P1 (2026-07-02)

## Stato

Tutti i 5 P1 sono stati censiti sul commit `c856387`.
Il Feature Freeze V0.1 è attivo (2026-06-29) — solo fix di build, test deterministici,
rimozione legacy path, consumer SDK, e allineamento documentazione sono consentiti.

## P1 #1 — Run multi-font: fallimento parziale silenzioso

**File:** `src/text/text_run_builder.cpp` (funzione `compile_text_layout`)

**Bug attuale:** Quando HarfBuzz fallisce su un singolo `ResolvedTextRun`, `shape_resolved_run()` restituisce `PlacedGlyphRun{}` vuoto. Il check post-shape `if (merged.glyphs.empty() && !para_text.empty())` rileva il problema SOLO quando TUTTI i run sono vuoti. Se run A produce glifi e run B fallisce, il merged ha glifi → nessun errore → il testo di run B sparisce silenziosamente.

**Azioni:**
1. [ ] Mantenere risultato per run: `vector<Result<PlacedGlyphRun, TextRunError>>` invece di `vector<PlacedGlyphRun>`
2. [ ] Applicare policy esplicita configurabile: `fail_whole_paragraph` | `fallback_font` | `replacement_glyph` | `placeholder_diagnostic`
3. [ ] Rimuovere l'implicito "skip and continue" — mai saltare un run senza segnalazione
4. [ ] Aggiungere test: paragrafo con 3 run, run centrale fallisce → il test verifica che il layout sia Err o che il placeholder sia presente

**Categoria Feature Freeze:** ❌ Bloccato (nuova feature). Richiede baseline verde prima dell'implementazione.

---

## P1 #2 — Determinismo multilingua dipendente dalla macchina

**File:** `src/text/text_resolver.cpp` (fallback font list), `src/backends/text/CMakeLists.txt` (FriBidi opzionale)

**Bug attuale:**
- FriBidi è opzionale — senza di esso, Chronon3D usa fallback solo LTR
- Font fallback usa famiglie installate nel sistema: DejaVu Sans, Liberation Sans, Arial, Helvetica, DejaVu Serif, Times New Roman, Georgia
- Output diverso tra due VPS con set di font diversi

**Azioni:**
1. [ ] Rendere FriBidi obbligatorio nel profilo `production text` (fail del preflight RTL se assente)
2. [ ] Sostituire la lista di font system con asset risolti e versionati (`AssetRegistry`)
3. [ ] Aggiungere hash del font + versione librerie di shaping nel manifest del render
4. [ ] Aggiungere test deterministico multi-piattaforma che verifichi lo stesso hash su due configurazioni
5. [ ] Documentare il contratto di determinismo in `RELEASE_GATE.md`

**Categoria Feature Freeze:** ❌ Bloccato (richiede nuove feature e modifiche API). Parzialmente sbloccabile: il test deterministico (#4) rientra nella categoria 2 (test deterministici).

---

## P1 #3 — Stato globale e cache process-wide

**File:**
- `src/text/text_run.cpp:205` — `shared_text_layout_cache()` singleton statico
- `src/backends/text/text_rasterizer_render.cpp:152` — `blend2d_resources()` statico
- `src/backends/text/glyph_atlas.cpp:75` — `get_glyph_atlas()` statico
- `src/backends/text/text_render_resources.cpp` — `BLFontFaceCache`, `FreeTypeFaceCache`

**Bug attuale:** Cache globali modificabili violano l'invariante documentato "nessun singleton globale nel render". Problemi di isolamento tra job concorrenti, impossibilità di avere budget/policy per-job.

**Azioni:**
1. [ ] Definire la gerarchia `RenderSession → TextRenderResources → {FontFaceCache, LayoutCache, RasterCache, GlyphAtlas, ScratchPool}`
2. [ ] Migrare `TextLayoutCache` da singleton a membro di `TextRenderResources` (o `RenderSession`)
3. [ ] Migrare `Blend2DResources` da statico a membro di `TextRenderResources`
4. [ ] Migrare `GlyphAtlas` (get_glyph_atlas statico) a membro per-sessione
5. [ ] Aggiornare tutti i callsite di `shared_text_layout_cache()` → passare `TextRenderResources&`
6. [ ] Rimuovere `shared_text_layout_cache()` e `reset_shared_text_layout_cache()` dall'header pubblico
7. [ ] Test: due `RenderSession` parallele non condividono cache → hit/miss indipendenti

**Categoria Feature Freeze:** ❌ Bloccato (refactor architetturale maggiore).

---

## P1 #4 — Doppia pipeline testuale (TextShape vs TextRun)

**File:**
- Pipeline A (nuova): `TextDocument → TextRunLayout → TextRunShape → draw_text_run`
- Pipeline B (legacy): `TextShape → TextLayoutEngine::layout → rasterize_text_to_bl_image`
- `src/backends/text/text_rasterizer_render.cpp` — `rasterize_text_to_bl_image`
- `src/backends/software/processors/text/software_text_processor.cpp` — chiama `rasterize_text_to_bl_image`

**Bug attuale:** Due sistemi di shaping/layout/rasterizzazione coesistono. `TextRunNode` dichiara esplicitamente di essere distinto dalla pipeline shape-rasterizer. La pipeline legacy (`TextShape → TextLayoutEngine → rasterize_text_to_bl_image`) è ancora in produzione via `software_text_processor.cpp`.

**Azioni:**
1. [ ] Censimento completo di TUTTI i callsite di `rasterize_text_to_bl_image` e `TextLayoutEngine::layout`
2. [ ] Censimento completo di TUTTI i callsite di `TextRunNode` / `draw_text_run`
3. [ ] Per ogni callsite legacy, decidere: adapter verso TextRun, deprecazione, o responsabilità distinta documentata
4. [ ] Se deprecazione: aggiungere `[[deprecated]]` + warning a compile-time
5. [ ] Proteggere con architecture gate: `tools/check_architecture_boundaries.sh` deve fallire se vengono aggiunti nuovi callsite legacy
6. [ ] Rimuovere la pipeline legacy dopo il periodo di deprecazione

**Categoria Feature Freeze:** ⚠️ Parzialmente consentito: il censimento (#1, #2) e l'architecture gate (#5) sono nella categoria 3 (rimozione legacy path). La rimozione effettiva (#6) richiede baseline verde.

---

## P1 #5 — Confine Core/Video contraddittorio ✅ RISOLTO

**File:** `src/CMakeLists.txt`

**Fix applicato:** Rimosso `target_link_libraries(chronon3d_core INTERFACE chronon3d_backend_video)` dal blocco `if(CHRONON3D_ENABLE_VIDEO)`. Il commento diceva che video doveva essere linkato via `chronon3d_software`, non `chronon3d_core`, ma il codice faceva l'opposto. Ora il contratto e il grafo CMake sono allineati.

**Verifica:** `chronon3d_software` linka già `chronon3d_backend_video`. `chronon3d_pipeline` linka entrambi `chronon3d_core` + `chronon3d_software`. Nessun consumer perde l'accesso al backend video.

**Categoria Feature Freeze:** ✅ Consentito (fix di build, categoria 1).

---

## Ordine di esecuzione consigliato

| Step | P1 | Azione | Categoria Freeze |
|------|-----|--------|-----------------|
| 1 | #5 | CMake fix | ✅ Fatto |
| 2 | #4 | Censimento callsite legacy | ⚠️ Consentito |
| 3 | #4 | Architecture gate anti-nuovi-callsite-legacy | ⚠️ Consentito |
| 4 | #2 | Test deterministico multi-piattaforma | ⚠️ Consentito |
| 5 | #1 | Per-run Result + policy esplicita | ❌ Post-baseline |
| 6 | #3 | RenderSession → TextRenderResources | ❌ Post-baseline |
| 7 | #2 | FriBidi obbligatorio + asset versionati | ❌ Post-baseline |
| 8 | #4 | Rimozione pipeline legacy | ❌ Post-baseline |
