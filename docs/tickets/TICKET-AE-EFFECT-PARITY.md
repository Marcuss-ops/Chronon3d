# TICKET-AE-EFFECT-PARITY — Effetti AE nativi mancanti (famiglia P2)

## Stato
PARTIAL (2026-10-28). Provenienza: gap analysis "AE + Premiere parity"
(MotionNations 30 video). Stato registrato in `docs/stato/chronon3d.md` §13.

## TODO — cosa manca (agg. 2026-10-28)

- [ ] Implementare i 4 effetti rimanenti (`stretch_warp`, `min_max`, `linear_wipe`, `lens_flare`) secondo lo scope 1–7. `posterize` e `color_map` sono a catalogo, processor, pipeline software, route Vulkan strict-native e (posterize) PixelProgram; i test sono presenti, verifica complessiva ancora da eseguire.
- [x] `color.posterize` (valore catalogo 56): fusibile e quantizza RGB straight in 2–256 livelli, preservando alpha; routing Vulkan nativo e opcode PixelProgram 13 aggiunti (2026-10-28).
- [x] `color.color_map` (valore catalogo 57): cinque stop RGB equispaziati sulla luminanza più `amount` 0–1 animabile, alpha preservata; processor software, stage fuso, mode Vulkan nativo 12 e test CPU/software (2026-10-28).
- [x] Eliminato lo scostamento `kEffectTypeCount`: capacità derivata dal massimo valore X-macro del catalogo (55) + regressione sullo slot `LottieTritone`; build `chronon3d_dev_fast` e test runtime mirato verdi (2026-10-28).
- [x] Il census strict-GPU segue il registry dinamico: il conteggio fisso 49 è rimpiazzato dal confronto con `kNativeRoutes.size()` dopo l'aggiunta dei route Posterize/ColorMap; `chronon3d_render_graph_tests` 1/1 CTest PASS (2026-10-28).
- [ ] Indipendente dal P1: può procedere subito.

## Goal

Coprire gli effetti nativi After Effects usati ripetutamente nei tutorial che
non hanno ancora un equivalente nel catalogo (55 effetti in
`include/chronon3d/effects/effect_catalog.def`):

| Effetto AE (video) | Nuovo effetto | Base di riuso |
|---|---|---|
| CC Scale Wipe — pixel stretching (#13) | `distort.stretch_warp` | kernel `WaveWarp`/`FoldWarp` |
| Posterize (#12 toon shading) | `color.posterize` | `LuminanceResponse` |
| CC Toner — colormap 5 stop (#12) | `color.color_map` | `CubeLUT`/`3D LUT` |
| Min Max (#18 testo 3D falso estruso) | `stylize.min_max` (erode/dilate) | pass scratch di `Halation` |
| Linear Wipe (#5 documentary) | `transition.linear_wipe` effetto layer | kernel `Wipe` di `clip_transition_node.cpp` |
| Lens Flare (#14 documentary opening) | `light.lens_flare` | `LightRays`/`AnamorphicStreak` |

## Scope (questa repo)

Per ogni effetto, ingresso canonico già collaudato (famiglia
`TICKET-POSTPROCESS-FX`, risolto):

1. `effect_catalog.def` (X-macro: valori, params type, id, category, stage,
   domain, alpha/dirty/bounds policy, temporal, fusible).
2. Params struct + `effect_ids.hpp`.
3. Processor software in `src/backends/software/processors/`.
4. Routing Vulkan strict-native (o `fusible` nella ColorPipeline se pointwise).
5. Decoder: entry in `render_plan_decoder_effects.cpp` (`kEffectDecoders`) e
   coverage in `src/render_plan/effect_kind_coverage.hpp` (static_assert su
   `EffectKind`).
6. **Allineamento `kEffectTypeCount`** in
   `include/chronon3d/runtime/job/job_session.hpp`: completato separatamente
   derivando la capacità dal valore massimo in `effect_catalog.def`; regressione
   su `LottieTritone` (valore 55) in `tests/runtime/test_render_session_reset_and_isolation.cpp`.
7. Test in `tests/effects/` (mapping catalogo, range parametri,
   identità/determinismo, fail-closed GPU).

## Fuori scope

- Effetti già coperti: Glow, Drop Shadow, Wave Warp, Displacement Map,
  Chromatic Aberration (il glitch RGB del video #3 è una recipe, non un nuovo
  effetto), gradienti (`4-Color Gradient`), Curves, Tint.
- Non reintrodurre Echo/FractalNoise/particelle: decisione 2026-10-22 già
  registrata in `docs/stato/chronon-motion3d.md` §2.

## Piano di chiusura passo-passo (agg. 2026-10-28)

Precondizione trasversale: feature freeze attiva — nessuna feature entra in
produzione finché la CI non è verde sullo SHA esatto (`docs/stato/README.md`);
i test fail-closed GPU richiedono l'host GPU NVIDIA.

1. [x] **Allineamento `kEffectTypeCount`**: capacità derivata da
   `effects::kEffectTypeMaxValue`, a sua volta generato dall'X-macro in
   `effect_catalog.def`; test runtime dimostra che lo slot `LottieTritone`
   (valore 55) è presente e registrato. Build e test mirato passati
   (2026-10-28).
2. **Per ognuno degli effetti rimanenti** (ordine suggerito:
   `stylize.min_max` → `transition.linear_wipe` → `distort.stretch_warp` →
   `light.lens_flare`; `color.posterize` e `color.color_map` sono implementati,
   verifica complessiva ancora da eseguire),
   ingresso canonico già collaudato della famiglia `TICKET-POSTPROCESS-FX`:
   a. riga in `effect_catalog.def` (X-macro: params type, id, category,
      stage, domain, alpha/dirty/bounds, temporal, fusible);
   b. params struct + `effect_ids.hpp`;
   c. processor software in `src/backends/software/processors/` (riuso dei
      kernel indicati nella tabella del Goal);
   d. routing Vulkan strict-native (o `fusible` nella ColorPipeline se
      pointwise);
   e. decoder: `kEffectDecoders` in `render_plan_decoder_effects.cpp` +
      coverage in `src/render_plan/effect_kind_coverage.hpp` (static_assert su
      `EffectKind`).
   Verifica per effetto: test in `tests/effects/` (mapping catalogo, range
   parametri, identità/determinismo, fail-closed GPU).
3. **Non-regressione fingerprint** — le invarianti di fusione dei programmi
   esistenti devono restare stabili.
   Verifica: i golden/fingerprint esistenti non cambiano.
4. **Chiusura formale** — registry coerente N/N strict-native, CTest verde;
   spuntare `## Accettazione`; `## Stato` → `COMPLETE`; spostare il file in
   `docs/tickets/archive/`; aggiornare `docs/stato/chronon3d.md` §13.

## Accettazione

- [ ] Registry effetti coerente: tutti i 6 nuovi effetti strict-native o
      fusibili come dichiarato, coverage completa e `kEffectTypeCount`
      derivato dal catalogo (questa parte è già implementata/testata).
- [ ] Ogni effetto deterministico e testato su software + Vulkan.
- [ ] Nessuna regressione sulle fingerprint dei programmi esistenti (le
      invarianti di fusione restano stabili).
- [ ] CTest verde.

## Dipendenze

Nessuna. Indipendente dal P1, può procedere subito.

## Riferimenti

- `Chronon3d/docs/tickets/archive/TICKET-POSTPROCESS-FX.md` (stesso ingresso
  catalogo, risolto).
