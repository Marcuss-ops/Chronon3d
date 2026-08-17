# TICKET-TEXT-OVERSIZED-CLIP-REGRESSION-TEST — test/regression dedicato al clipping testo oversized (NON BLOCKING)

## Stato
OPEN

## Priorità
P3 — **NON BLOCKING**: ticket di test/regression **separato** dal warning (`TICKET-TEXT-OVERSIZED-CLIP-WARN`). Non tocca il percorso RenderingGen stabile: solo test Chronon + fixture.

## Problema
Non esiste un lock testuale che impedisca **regressioni silenziose** sul comportamento di clipping di testo oversized nel percorso render-plan (CLI, lo stesso usato dal golden canary E2E):

1. `tests/text/test_text_clip_oversized.cpp` asserisce la containment dell'ink nel box con **4 `WARN`** (non `CHECK`) a causa della KNOWN LIMITATION `TICKET-TEXT-CLIP-BOX` — oggi il test **passa anche se il clipping non viene enforced** (spill di ink fuori box tollerato).
2. Il warning `[text-vis] CLIP_DROPS_INK` osservato sul golden canary è **diagnostico-only** (gated `CHRONON3D_BUILD_DIAGNOSTICS`, dedup warn-once): nessun test verifica che un layer oversized **debba** produrre il warning, né che un layer contenuto **non debba** produrlo.
3. Nessuna copertura del percorso **render-plan JSON** (il path che PipelineGen/RenderingGen usano in produzione: `render_plan::decode_render_plan` → layer con `box`/`preset`/`font`): i test esistenti costruiscono la scena via DSL (`composition(...)`), non via plan decodificato.

## Evidenza
- `tests/text/test_text_clip_oversized.cpp` — 4 `WARN` sulla bbox contenuta (lines 83–86) + commento KNOWN LIMITATION `TICKET-TEXT-CLIP-BOX` (line 81).
- `tests/text_golden/text_clip/text_clip_bounds.cpp` — cluster Clip 01–06: assertion numeriche + diagnostic, ma nessun golden seedato (vedi `TICKET-TEXT-CLIP-GOLDENS-01-05`).
- `src/text/text_visibility_reporting.cpp` + `src/text/text_visibility_audit.cpp` — invariante `clip_contains_visible_ink` (containment con tolleranza).
- Percorso produzione: `src/render_plan/render_plan_decoder.cpp` + `src/render_plan/render_plan_compiler.cpp` (layer text con `preset`/`box`/`font`).
- Golden canary: `RenderingGen/infra/e2e/run-golden-overlay.sh` (la WARN è comparsa nel worker log su un layer del payload congelato).

## Impatto
- Nessuno sul percorso stabile (test-only ticket).
- Senza lock: una futura modifica all'enforcement del clip (o al layout) può introdurre spill di ink / warning spariti / bbox divergenti senza che nessun test fallisca.

## Confine
- **Solo test Chronon** (TEST_CASE + fixture): nuovo test render-plan-driven + promozione delle WARN esistenti.
- **ESCLUSO**: modifiche al percorso RenderingGen stabile (worker, queue, objectstore, `run-golden-overlay.sh`, payload golden). Il payload `testdata/golden/` di RenderingGen resta byte-identico.
- Escluso: fix dell'enforcement (appartiene a `TICKET-TEXT-CLIP-BOX` / policy in `TICKET-TEXT-OVERSIZED-CLIP-WARN`).

## Soluzione accettabile
1. **Nuovo TEST_CASE render-plan-driven** (es. `tests/text/test_text_clip_oversized_plan.cpp`): decodifica un plan JSON minimale con un layer text oversized (box piccolo, font dichiarato, preset o `overflow=Clip`) e verifica:
   - l'ink renderizzato **non** eccede il box oltre la tolleranza (`kTextAuditBBoxTolerance`), una volta che la policy di `TICKET-TEXT-OVERSIZED-CLIP-WARN` è definita;
   - il warning `[text-vis] CLIP_DROPS_INK` viene emesso **esattamente una volta** per il layer oversized (warn-once) e **mai** per un layer contenuto;
   - output deterministico (stesso frame → stessi byte alpha-bbox).
2. **Promuovere le 4 `WARN` di `test_text_clip_oversized.cpp` a `CHECK`** quando la policy/enforcement di `TICKET-TEXT-CLIP-BOX` atterra (promozione dipendente, non ora).
3. **Opzionale**: golden PNG del caso oversized sotto `tests/text_golden/text_clip/goldens/` (post-enforcement, allineato a `TICKET-TEXT-CLIP-GOLDENS-01-05`).

## Criteri di accettazione
- Nuovo TEST_CASE render-plan-driven PASS su build con diagnostics ON (emissione warn verificata) e OFF (nessun crash, comportamento invariato).
- Le 4 WARN esistenti promosse a CHECK solo quando `TICKET-TEXT-CLIP-BOX` è risolta (nessuna promozione prematura = nessun falso rosso).
- Zero modifiche a RenderingGen: `git diff RenderingGen/` vuoto.
- Cluster golden esistenti (`text_golden/text_clip`, 45/45 assertion RenderPlan/Budget) verdi.

## Linkage
- **Parent**: [`TICKET-TEXT-OVERSIZED-CLIP-WARN`](TICKET-TEXT-OVERSIZED-CLIP-WARN.md) (il warning; questo ticket è il lock testuale separato).
- Known limitation: `TICKET-TEXT-CLIP-BOX` (reference in `test_text_clip_oversized.cpp:81`; promozione WARN→CHECK dipende dalla sua risoluzione).
- Sibling golden: [`TICKET-TEXT-CLIP-GOLDENS-01-05`](TICKET-TEXT-CLIP-GOLDENS-01-05.md) (P1, seed golden cluster clip).
- Percorso produzione sotto test: `src/render_plan/render_plan_decoder.cpp`, `src/render_plan/render_plan_compiler.cpp`.
- Percorso stabile da NON toccare: `RenderingGen/` (golden canary congelato).
