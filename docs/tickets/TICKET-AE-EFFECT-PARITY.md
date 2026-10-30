# TICKET-AE-EFFECT-PARITY — Effetti AE nativi mancanti (famiglia P2)

## Stato
PARTIAL (updated 2026-10-29). Provenienza: gap analysis "AE + Premiere parity"
(MotionNations 30 video). Stato registrato in `docs/stato/chronon3d.md` §13.

`color.posterize` and `color.color_map` remain implemented as recorded below.
The existing uncommitted implementations of `transition.linear_wipe`,
`stylize.min_max`, `distort.stretch_warp`, and `light.lens_flare` now pass local
software/catalog/hash/decoder/compiler verification (2026-10-29). All four
now have strict-native Vulkan routes and pass the focused software/Vulkan
pixel matrix (16,108 assertions; checkpoint below). This closes the local
four-effect parity implementation, not full certification: CI same-SHA remains
NOT RUN.

## TODO — cosa manca (agg. 2026-10-28)

- [x] `stylize.min_max`: contratto bounded, morfologia RGBA separabile software, mapping/range/identità/determinismo e GPU capability fail-closed verificati localmente.
- [x] `transition.linear_wipe`: software, feather/angolo/clip/endpoint, animazione, decoder/compiler e hash verificati localmente; GPU capability fail-closed verificata.
- [x] `distort.stretch_warp`: pixel stretching direzionale tramite Sampler2D canonico (non un alias WaveWarp/FoldWarp), catalogo/decoder/compiler e test software verificati; GPU capability fail-closed verificata.
- [x] `light.lens_flare`: soglia/halo/ghost/streak bounded, catalogo/decoder/compiler, alpha premoltiplicata e determinismo software verificati; GPU capability fail-closed verificata.
- [x] Implementare e verificare localmente la parità Vulkan dei quattro effetti sopra: route NativeZeroReadback, confronto pixel e input invalidi PASS. Certificazione completa e CI same-SHA restano separate.
- [ ] Per ogni effetto completato: verificare mapping, range, identità, determinismo, alpha/dirty bounds e comportamento GPU fail-closed o nativo; CI same-SHA resta gate separato.
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

## Verifica locale del lavoro già iniziato — 2026-10-29

Base `d37725c97b1c47c133d62c6f5d6a4cd8fdca2471`, worktree dirty;
nessun commit/push o certificazione same-SHA. Preset esistente
`build/chronon/linux-video-test`: Debug, Vulkan ON, SVG OFF, Linux x86_64.
I sorgenti dei quattro effetti erano già modificati all'ingresso di questa
sessione; questo checkpoint ne verifica lo stato, non ne attribuisce una nuova
implementazione.

- Build: `cmake --build build/chronon/linux-video-test --target chronon3d_effects_tests chronon3d_render_graph_tests chronon3d_c_abi_tests -j16` — PASS, exit 0. Log locale: `.tmp/ae-parity-closeout-build.log`.
- Binary freshness verificata prima dei test. `ctest --test-dir build/chronon/linux-video-test -R '^chronon3d_(effects|render_graph)_tests$' --output-on-failure` — PASS, 2/2 suite, 91.79 s. Render graph: 439/439 casi, 19,474 assertion; effects: 304/304 casi, 5,540,351 assertion. Log locale: `.tmp/ae-parity-closeout-tests.log`.
- `build/chronon/linux-video-test/tests/chronon3d_c_abi_tests --test-case='AE parity*' --no-skip --no-colors` — PASS, 2/2 casi selezionati, 44/44 assertion. Copre decode, range, content fingerprint e compilazione ai tipi canonici; NON è un run completo della suite C ABI né una prova prepared-render pixel. Log: `.tmp/ae-parity-closeout-cabi.log`.
- `python3 tools/check_architecture.py` — PASS, 214/214 regole; log `.tmp/ae-parity-architecture.log`.
- Catalogo corrente: 61 effetti, 51 route native e 10 Unsupported esplicite. I test esistenti di fingerprint/fusione della suite passano senza aggiornamento dei golden.

I log sono artefatti locali gitignored, non certificati pubblicati. Restano
aperti parità GPU dei quattro effetti, golden di accettazione e CI same-SHA.

## Parità Vulkan dei quattro effetti — 2026-10-29

Checkpoint successivo alle verifiche software sopra (che restano storiche).
LinearWipe usa il kernel color-transform con reveal premoltiplicato;
MinMax due pass separabili RGBA con scratch nativo e bordo trasparente;
StretchWarp campionamento bilineare clamp; LensFlare threshold/halo/ghost/streak
con source-over premoltiplicato. Riutilizzati lowering e dispatch canonici;
nessun fallback CPU. Capability census: 55 route native e 6 Unsupported.

- Build `cmake --build build/chronon/linux-video-test --target chronon3d_backend_registry_tests chronon3d_render_graph_tests chronon3d_cli -j16` PASS.
- Ricompilazione finale backend registry con `-j16` PASS (`.tmp/ae-native-rebuild.log`).
- Con `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json`, binario backend
  registry fresco, `--test-case='Vulkan AE effects*' --no-skip --no-colors`:
  **1 caso / 16,108 assertion PASS**, tutte le quattro subcase eseguite.
  Log `.tmp/ae-native-parity-test.log`, exit 0. Host NVIDIA RTX A4000.
- Matrice: LinearWipe angoli 0/45/90/180/270/360, progress 0/0.5/1,
  feather 0/0.5; MinMax radius 0/1/64 e min/max; StretchWarp cinque angoli
  e stretch 0/0.5/1; LensFlare default e soglie 0/0.8/1.
  Ogni combinazione confronta tutti i canali RGBA con il processor software
  registrato, full frame e clip, quindi una replica byte-identica; NaN/Inf
  e range invalidi falliscono strict-native.
- Il primo tentativo LinearWipe ha fallito `vkCreateInstance` (-9) dopo 1,716
  assertion verdi: il test creava un device per ogni campione. Il test finale
  riusa un backend per subcase senza eliminare casi o indebolire assertion.
- Test graph capability/hash/lifetime/dispatch/DOF mirati: 9 casi e 362
  assertion PASS (`.tmp/ticket-graph-focused.log`).

Worktree dirty su base `d37725c97`; log locali gitignored, nessun commit/push.
Nessun run completo backend registry dopo questa modifica, golden di produzione
né CI same-SHA: il ticket resta PARTIAL per questi gate, non per route Vulkan
mancanti. I riferimenti Unsupported nelle verifiche precedenti sono storici.

## Dipendenze

Nessuna. Indipendente dal P1, può procedere subito.

## Riferimenti

- `Chronon3d/docs/tickets/archive/TICKET-POSTPROCESS-FX.md` (stesso ingresso
  catalogo, risolto).
