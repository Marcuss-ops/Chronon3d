# 06 — Renderer plan: alleggerimento di `SoftwareRenderer`

> Stato: pre-stabile. Piano operativo per portare `SoftwareRenderer` da
> orchestratore-impasto a thin facade. Complementa (non duplica)
> [`../refactor-roadmap/07-software-backend-completion.md`](../refactor-roadmap/07-software-backend-completion.md)
> (WP-7): WP-7 fissa la *missione completa* di separazione; questo
> documento propone la *fase esecutiva* che rende quella missione
> chiudibile senza regressioni.

Stato: [`../STATUS.md`](../STATUS.md) · Piano operativo:
[`../NEXT_STEPS.md`](../NEXT_STEPS.md) · Vincoli:
[`../ANTI_DUPLICATION_RULES.md`](../ANTI_DUPLICATION_RULES.md) · Work
Package di riferimento: WP-7.

---

## 1. Problema

`SoftwareRenderer` oggi è ancora un ibrido:

- eredita sia `Renderer` che `graph::RenderBackend`
  ([`include/chronon3d/backends/software/software_renderer.hpp:121`](../../include/chronon3d/backends/software/software_renderer.hpp));
- continua a tenere metodi `RenderBackend` (`apply_blur`,
  `composite_layer`, `apply_effect_stack`, `apply_per_pixel_dof`,
  `draw_node`, `draw_text_run`, `capabilities`, `framebuffer_pool`,
  `counters`) che semplicemente inoltrano a
  `m_runtime->backend()`;
- ha un header pubblico da 467 righe che include direttamente
  `chronon3d/runtime/render_runtime.hpp`,
  `chronon3d/scene/model/render/render_node.hpp`,
  `chronon3d/scene/model/layer/layer.hpp`,
  `chronon3d/scene/model/core/scene.hpp`, `chronon3d/simd/kernels.hpp`,
  `chronon3d/timeline/composition.hpp`, `chronon3d/core/config.hpp` —
  venti+ header interni propagati a ogni TU che includa il renderer;
- i processor (`ShapeProcessor::draw`) e `software_text_processor.cpp`
  continuano a richiedere `SoftwareRenderer&` invece di un contratto
  stretto (`RenderBackend&` o servizio), vincolando la migrazione del
  grafo al tipo concreto.

Questi elementi bloccano il raggiungimento di WP-7 PR 7.4–7.5
("`SoftwareRenderer` is no longer a backend", "Pipeline contains no
backend-to-renderer cast", "Backend behavior is testable without
renderer construction") e ostacolano WP-8 PR 8.5 ("Standard SDK facade
exposes no software implementation types").

## 2. Stato reale (audit 2026-06-21)

| Aspetto | Stato | Evidenza |
|---|---|---|
| `RenderRuntime` detiene cache + executor + scheduler + cataloghi + `SoftwareBackend` | ✅ | TICKET-011 + WP-8 PR 8.0 |
| `SoftwareRenderer` eredita `RenderBackend` | 🔴 | `software_renderer.hpp` riga 121 |
| `SoftwareBackend` esiste e implementa le primitive di rendering | ✅ (parziale) | `software_backend.{hpp,cpp}` |
| `ShapeProcessor::draw(SoftwareRenderer&, ...)` firma | 🔴 | `src/backends/software/shape_processor.hpp` |
| `dynamic_cast<SoftwareRenderer*>` nel pipeline | 🔴 | da inventariare (vedi R1) |
| Header slimming (FontEngine fuori dal TU-include) | ✅ | WP-8 PR 8.1; altri 19+ header ancora transitivi |
| `SoftwareBackend` viene istanziato dalla catena runtime | ✅ | `RenderEngine::Impl::attach_backend()` |
| Process-wide runtime e resolver globali | 🔴 | WP-8 PR 8.2 ancora aperto (vedi sez. 6) |

## 3. Non-goal

Questo piano **non** introduce nuovi registry, nuovi resolver, nuove
cache o nuove facciate pubbliche (regole 1, 2, 4, 8, 9 di
[`ANTI_DUPLICATION_RULES.md`](../ANTI_DUPLICATION_RULES.md)).
In particolare:

- non introduce un secondo `RenderBackend` per il software renderer;
- non sposta `Blend2D` o `FreeType`/`HarfBuzz` in un nuovo modulo —
  resta `chronon3d_backend_software` come casa canonica;
- non cambia l'API pubblica `RenderEngine`: il consumer vede solo
  `Chronon3D::SDK`.

## 4. Obiettivo

Alla chiusura del piano:

1. `SoftwareRenderer` non eredita più `graph::RenderBackend`; le 8
   override di `RenderBackend` scompaiono dal renderer e vivono solo in
   `SoftwareBackend`.
2. `pipeline::render_scene_via_graph` /
   `pipeline::render_composition_frame` chiamano
   `RenderRuntime::backend()` per le primitive di rendering, mai
   `*dynamic_cast<SoftwareRenderer*>(&backend)`.
3. `ShapeProcessor::draw(RenderBackend&, ...)` firma contratto —
   `SoftwareRenderer&` viene eliminato da tutti i processor.
4. L'header `software_renderer.hpp` scende ≤ 200 righe e non include
   più di 6 header *non* `backends/software/`.
5. Una suite di test di confine garantisce che il pipeline non includa
   `software_renderer.hpp` e che nessun processor firmi
   `SoftwareRenderer&`.

## 5. Lavoro proposto (PR ordinate)

Le PR sono strette per poter chiudere [02](../stabilization-plan/02-determinism.md)
e [03](../stabilization-plan/03-execution-scope-and-precomp.md) in
parallelo senza regresione.

### R1 — Inventario e baseline `SoftwareRenderer`

> Stesso spirito di WP-7 PR 7.0 ma con deliverable concreto.

- [ ] Produrre un classificatore (`tools/audit_software_renderer.sh` o
      sezione report) che per ogni metodo pubblico/protetto di
      `SoftwareRenderer` elenchi: *categoria* (backend / orchestrazione
      / settings / telemetry / cache / servizio), *destinazione
      naturale* (`SoftwareBackend` / `RenderRuntime` / eliminazione),
      *consumer noti*.
- [ ] Elencare ogni firma `SoftwareRenderer&` nei processor e helper:
      `ShapeProcessor::draw`, `software_text_processor.cpp`,
      `text_run_processor.cpp`, qualunque `to_local_clip` /
      `clipped_area` duplicato.
- [ ] Elencare ogni `dynamic_cast<SoftwareRenderer*>` o `static_cast`
      verso `SoftwareRenderer` in `src/render_graph/**` e
      `src/runtime/**`.
- [ ] Identificare i 20+ include interni transitivi di
      `software_renderer.hpp` e per ciascuno marcare: *serve davvero
      qui?* sì/no, *altrimenti spostato dove?*.
- [ ] Pubblicare l'inventario in
      `docs/audits/2026-06-software-renderer-inventory.md` con SHA di
      baseline.

Done quando l'inventario copre 100% dei metodi ed è linkato da questo
documento.

### R2 — Migrazione dei processor da `SoftwareRenderer&` a servizi stretti

> Specchio di WP-7 PR 7.1 + PR 7.3, ma con un perimetro più piccolo:
> solo ciò che serve ai test veloci (`chronon3d_tests_fast`).

- [ ] Introdurre `SoftwareBackendServices`
      (`include/chronon3d/backends/software/backend_services.hpp`)
      raggruppando: `RenderCounters&`, `RenderSettings`,
      `cache::FramebufferPool&`, `renderer::SoftwareRegistry*`,
      `image::ImageRenderer*`, `text::FontEngine*`
      (solo se `CHRONON3D_HAS_BACKEND_TEXT`), `AssetResolver&`.
- [ ] Introdurre
      `SoftwareProcessorContext` con i soli campi che
      `ShapeProcessor::draw` consuma: `counters`,
      `settings`, `framebuffer_pool`, `registry`, `font_engine`,
      `image_renderer`, `resolver`.
- [ ] Aggiornare `ShapeProcessor::draw` a
      `void draw(SoftwareProcessorContext&, Framebuffer&, const RenderNode&, const RenderState&, const Camera&, int, int)`.
- [ ] Adattare `processor->draw(*this, ...)` (oggi verso
      `SoftwareRenderer&`) in
      `processor->draw(ctx_servizi, ...)` propagando `m_runtime` come
      fonte di tutti i servizi.
- [ ] Adattare `software_text_processor.cpp` e `text_run_processor.cpp`
      alla nuova firma (rimuovere ogni lettura/scrittura di campi
      privati del renderer).
- [ ] Test mirato: mismo golden hash prima/dopo per
      `tests/renderer/test_text_material.cpp` (suite text) e
      `tests/render_graph/test_render_scene_via_graph.cpp`.

Done quando `grep -R "SoftwareRenderer&" src/backends/` non trova alcun
uso non-essenziale e i golden text/grafico restano bit-for-bit uguali.

### R3 — Rimozione della doppia identità renderer/backend

> Corrisponde a WP-7 PR 7.4 ma in due sotto-PR per ridurre il rischio.

#### R3a — Spegnimento delle override `RenderBackend` sul renderer

- [ ] `SoftwareRenderer::apply_blur` → rimosso (chiama `backend()`).
      Aggiornare `RenderGraphContext` perché punti a `RenderBackend&`.
- [ ] `SoftwareRenderer::composite_layer`,
      `apply_effect_stack`, `apply_per_pixel_dof` → rimossi
      (inoltratori puri).
- [ ] `SoftwareRenderer::draw_node`, `draw_text_run` →
      rimosso/riclassificato. Le primitive restano su
      `SoftwareBackend` (per `draw_node` occorre R2 completato).
- [ ] `SoftwareRenderer::framebuffer_pool`/`counters` override →
      rimossi; il contract `RenderBackend` esposto dal runtime è il
      solo.
- [ ] Aggiornare `RenderNode`/`RenderGraphContext` perché leggano
      `runtime().backend()` anziché `*this`.

Done quando `grep -E "class SoftwareRenderer.+RenderBackend" include/`
non trova più la doppia eredità.

#### R3b — `SoftwareRenderer` non eredita più `RenderBackend`

- [ ] Togliere `public graph::RenderBackend` da `SoftwareRenderer`.
- [ ] Rimuovere ogni `override` non più richiesto; il renderer tiene
      solo le signature `Renderer` (interfaccia di orchestrazione).
- [ ] Tenere `Renderer` come facciata di orchestrazione per
      `RenderEngine::Impl` fintanto che serve; quando possibile
      ridurla a un piccolo `RenderJobFacade` su `RenderRuntime`.
- [ ] Test di confine: il pipeline `include/render_graph/pipeline/render_pipeline.hpp`
      non include `software_renderer.hpp` (greppabile,
      [`tools/check_architecture_boundaries.sh`](../../tools/check_architecture_boundaries.sh)).

Done quando `SoftwareRenderer : public Renderer` (singola catena) e
l'inventario R1 è azzerato per la colonna *backend*.

### R4 — Header slimming

> Riprende la logica di TICKET-011 / WP-8 PR 8.1 applicata a tutti gli
> altri include transitivi.

- [ ] Sostituire `chronon3d/scene/model/render/render_node.hpp`,
      `chronon3d/scene/model/layer/layer.hpp`,
      `chronon3d/scene/model/layer/layer_effect.hpp`,
      `chronon3d/scene/model/core/effect_stack.hpp`,
      `chronon3d/scene/model/camera/camera_2_5d.hpp`,
      `chronon3d/scene/model/core/scene.hpp`,
      `chronon3d/scene/model/camera/camera.hpp`,
      `chronon3d/timeline/composition.hpp`,
      `chronon3d/core/types/frame.hpp`,
      `chronon3d/simd/kernels.hpp`,
      `chronon3d/math/transform.hpp`,
      `chronon3d/math/renderer_state.hpp` con forward declarations
      (`class RenderNode;`, `struct Frame;`, ecc.).
- [ ] Lasciare inclusi nel header solo i tipi che compaiono in firma
      pubblica: `runtime::RenderRuntime&` (già), `Config`,
      `RenderSettings`, `RenderCounters`, `CompositionRegistry*`,
      `Framebuffer`, `TextRunShape`, `Mat4`, le forward decl in stile
      `class FontEngine;`.
- [ ] Tenere gli unici `#include` pesanti dietro guard diagnostici:
      `CHRONON3D_BUILD_DIAGNOSTICS` per gli overlay, `CHRONON3D_USE_BLEND2D`
      per le facciate blend2d, `CHRONON3D_HAS_BACKEND_TEXT` per
      `Chronon3d/backends/text/text_rasterizer_utils.hpp`.
- [ ] Misurare il delta: `wc -l include/chronon3d/backends/software/software_renderer.hpp`
      e `cppcheck --enable=unusedFunction src/backends/software/software_renderer.cpp`.

Done quando l'header è ≤ 200 righe, ≤ 6 include non-locali, e ogni TU
del progetto che lo includeva continua a compilare senza warning di
simboli mancanti.

### R5 — Test di confine del backend

> WP-7 PR 7.5, riportato come deliverable concreto di questo piano.

- [ ] Aggiungere a `tests/backends/software/`:
  - `test_software_backend_in_isolation.cpp` — costruisce un
    `SoftwareBackend` con mock services e verifica ogni
    `RenderBackend` override senza toccare `SoftwareRenderer`.
  - `test_pipeline_does_not_include_renderer.cpp` — compile-fail
    test: include solo `chronon3d/render_graph/pipeline/*.hpp` e
    dichiarare assembly-level (`asm("...")` o commento link-time)
    che il simbolo `chronon3d::SoftwareRenderer` non è referenziato.
  - `test_processor_signature_is_narrow.cpp` — compile-time test
    (`static_assert`) che nessun processor espone firma con
    `SoftwareRenderer&`.
- [ ] Aggiungere uno smoke E2E: `tests/renderer/test_renderer_is_thin.cpp`
      — istanzia `SoftwareBackend` + `SoftwareRenderer`, esegue
      `render_frame`, verifica che i counter di `SoftwareBackend`
      (non del renderer) vengano incrementati.
- [ ] Aggiornare `tools/check_architecture_boundaries.sh` con una
      regola: `incidenza di backends/software/software_renderer.hpp`
      vietata in `src/render_graph/**` e
      `src/backends/software/processors/**` — fallimento bloccante.

Done quando i 5 test passano in `chronon3d_tests_fast` + `linux-ci` e
`check_architecture_boundaries.sh` li cita come invarianti.

### R6 — Chiusura processo-wide runtime globals (interlock con WP-8 PR 8.2)

> Non è il focus di questo piano ma è bloccante per i gate R3.R4 (un
> renderer "alleggerito" deve poggiare su un runtime senza
> fallback globale).

- [ ] Verificare che, dopo R3/R4, la rimozione di `g_active_runtime`,
      `set_active_runtime()`, `active_runtime()`,
      `g_process_wide_assets_root`,
      `g_deep_resolver_mirror` e `typed_resolver_for_deep_code()`
      (WP-8 PR 8.1–8.2) non rompa la compilazione di
      `SoftwareRenderer` né dei suoi test.
- [ ] Test di isolamento in `tests/runtime/` (mirror del WP-8 PR 8.2):
      due `RenderRuntime` con radici diverse, stessa `SoftwareRenderer`
      factory non li contamina.

Done quando WP-8 PR 8.1–8.2 è chiuso e i test *software-renderer-thin*
restano verdi.

## 6. Test plan

Per ogni R: test mirato obbligatorio prima della chiusura della PR,
con golden hash o smoke E2E equivalente. Configurazione minima di
riferimento: preset `linux-lean-dev` (test veloci) + `linux-ci`
(install consumer). I test della sezione 5 (R5) **sono** i test di
accettazione del piano.

| PR | Test gates | Sotto-suite |
|---|---|---|
| R1 | inventario classificato, SHA di baseline registrato | `tools/audit_software_renderer.sh` |
| R2 | golden text + golden render invariati | `tests/renderer/test_text_material.cpp`, `tests/render_graph/test_render_scene_via_graph.cpp` |
| R3a | nessuna regressione in `tests_fast` | `chronon3d_tests_fast` sotto `linux-lean-dev` |
| R3b | pipeline non include renderer + nessuna dual-eredità | R5 suite + grep |
| R4 | wc -l header ≤ 200, ≤ 6 include non-locali | `wc -l`, `tools/check_architecture_boundaries.sh` |
| R5 | 5 test verdi in `linux-ci` | nuova suite `tests/backends/software/` |
| R6 | due runtime isolati, risoluzione indipendente | suite WP-8 PR 8.2 |

## 7. Criteri di chiusura

Rispecchia la *"Regola di chiusura"* del
[`README.md`](README.md): codice, test e documentazione riportano lo
stesso stato.

- [ ] R1 → R6 merged in ordine.
- [ ] `SoftwareRenderer : public Renderer` (nessuna doppia catena).
- [ ] `SoftwareBackend` implementa ogni capability che `RenderCapabilities`
      pubblicizza (gate TICKET-007).
- [ ] `wc -l include/chronon3d/backends/software/software_renderer.hpp ≤ 200`.
- [ ] `grep -R "SoftwareRenderer&" src/backends/software/processors src/render_graph`
      → 0 risultati non-essenziali.
- [ ] `grep -R "dynamic_cast<SoftwareRenderer" src/ ` → 0 risultati.
- [ ] `tools/check_architecture_boundaries.sh` registra le nuove
      invarianti e fallisce se vengono violate.
- [ ] Suite di accettazione R5 verde su `linux-lean-dev` + `linux-ci`.
- [ ] Aggiornare `STATUS.md` (riga "SoftwareRenderer alleggerito"),
      `ROADMAP.md` (rimozione ticket storici se presenti),
      `NEXT_STEPS.md` (le sei sezioni P0 restano verdi o
      progrediscono), `CHANGELOG.md` (voce chiusura R6).
- [ ] Riferimento in [`../refactor-roadmap/07-software-backend-completion.md`](../refactor-roadmap/07-software-backend-completion.md):
      PR 7.5 "Add backend boundary tests" → delivered via R5.

## 8. Rischi e mitigazioni

| Rischio | Mitigazione |
|---|---|
| `ShapeProcessor::draw` rotto da R2 → regression in golden | Eseguire R2 prima di R3; suite golden pre-/post- su `linux-lean-dev`. |
| Test esistenti che leggono campi privati del renderer | TICKET storici da chiudere in R2; budget PR dedicato. |
| Header slimming rompe compilazione di TU "test-only" | R4 in due sotto-PR (R4a field-promotion, R4b include-prune). |
| R6 incrocia WP-8 PR 8.1/8.2 già chiusi | R6 è solo "verifica non-regressione"; non blocca R1–R5. |
| WP-7 PR 7.0–7.5 diverge da questo piano | Allineamento continuo via `STATUS.md` + riferimento esplicito in sezione 7. |

## 9. Segnale di completamento

Quando tutte le PR R1–R6 sono chiuse e i criteri di sezione 7 sono
verdi, marcare `SoftwareRenderer alleggerito` nel TODO generale di
[`README.md`](README.md). Quel punto della TODO generale è la prova
formale che questa fase è completa.
