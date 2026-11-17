# TICKET-SVG-CLI-RENDER-PLAN-LANE-20261116 — path SVG e animazioni operator sulla CLI

- **Stato:** DONE — 2026-11-16 (lane software; CI same-SHA resta esterna)
- **Ambito:** Chronon3d, `render --plan` CLI, `compiler::compile` → `SceneMaterializer`, path SVG importati e animazioni degli operatori path/SVG
- **Origine:** chiusura area SVG del 2026-11-16; sei gate path/motion falliscono mentre gli otto gate di testo passano
- **Parent:** [TICKET-SVG-RENDER-PLAN.md](TICKET-SVG-RENDER-PLAN.md)

## Sintomo misurato

Sulla CLI Release SVG-enabled (`build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli`)
gli otto gate raw di testo passano, ma questi sei falliscono:

| Gate | Osservazione |
|---|---|
| `check_svg_group_isolation_gate.py` | frame statico interamente trasparente |
| `check_svg_cli_frame_gate.py` | 0 pixel del control shape |
| `check_svg_composite_showcase_gate.py` | 31/31 frame identici |
| `check_svg_motion_animations_gate.py` | 17 predicati falliti |
| `check_svg_static_composite_raw_gate.py` | path SVG sparito |
| `check_svg_trim_sampled_raw_gate.py` | tutti i campioni trim a 0 |

**Risolto il 2026-11-16.** Tutti e sei i gate passano sullo stesso binario
Release con SVG ON, senza modificare gate o tolleranze: group isolation 10/10,
cli frame con hash endpoint distinti, composite 31/31 frame deterministici,
motion 17/17, static composite 132 px, trim `[0,0,67,96,96]` con repeat
byte-identico. Build: `cmake --build build/chronon/linux-video-release
--target chronon3d_cli -j12`, exit 0.

## Cosa manca davvero

### 1. Materializzazione di `imported_subpaths`

`prepare_render_plan` → `compiler::compile` → `SceneMaterializer::materialize_scene`
è la lane usata dalla CLI. Il case `ShapeContentTypePlan::Path` in
`src/compiler/scene_materializer.cpp` legge solo `shape.path` e ignora
`shape.imported_subpaths`, già popolato da `prepare_svg_layers`. Ogni layer con
`shape.asset` SVG produce un nodo path vuoto: il loader passa 59/59 casi, ma la
geometria importata non arriva al raster.

Manca la materializzazione di:

- comandi del subpath e `fill_rule`;
- fill solido/gradiente e stroke solido, con opacità e animazioni paint;
- operatori path (trim, repeater, morph, offset, zig-zag, round corners,
  pucker/bloat, twist, wiggle, merge/boolean);
- gruppi isolati e maschere vettoriali per elemento;
- trasformazioni SVG e morph target.

La logica completa esiste già in
`src/render_plan/render_plan_compiler_shape_imported.cpp` per il lane
RenderPlan: va riusata o va scelto quel lane come autorità, senza duplicare una
seconda semantica.

### 2. Sampling per-frame delle animazioni operator/SVG

`CompiledProgram` è immutabile e dichiara che i valori variabili per frame
appartengono al sampler/runtime. Invece
`render_plan_compiler_shape_path.cpp` valuta le animazioni degli operatori con
`ctx.effective_frame()` durante il compile, e `SceneMaterializer` non riceve un
frame. Il video ripete quindi il frame 0: misurati 31/31 frame identici su
composite showcase e tutti i campioni trim a 0.

Manca una delle due strade, da decidere esplicitamente:

- **A.** il runtime campiona per frame i track degli operatori e degli elementi
  SVG dentro il programma compilato; oppure
- **B.** la CLI ricompila il piano per ogni frame con il `FrameContext` corrente
  e materializza il risultato.

La strada B è la più vicina al codice RenderPlan esistente, ma va misurato il
costo per frame e va esclusa la cache statica. La strada A è più coerente con
`CompiledProgram` congelato, ma richiede di portare nel programma i track oggi
valutati al compile.

## Piano di chiusura

1. **Decisione di autorità** — scegliere A o B e registrarla nel ticket; vietato
   mantenere due semantiche divergenti tra CLI e lane RenderPlan.
2. **Geometria** — cablare `imported_subpaths` nella materializzazione scelta,
   con fill/stroke/gradienti, regole di riempimento, gruppi e maschere.
3. **Tempo** — cablare il sampling per frame di operatori e animazioni SVG;
   verificare che un trim/repeater/morph cambi davvero i pixel tra frame
   consecutivi e che il repeat sia deterministico.
4. **Gate** — rendere verdi i sei gate path/motion sullo stesso binario, senza
   cambiare tolleranze o fixture; mantenere verdi gli otto gate di testo.
5. **Evidenza** — registrare comandi, SHA, configurazione e output nel ticket
   padre e in `CURRENT_STATUS.md`.

## Criteri di accettazione

- [x] La lane CLI materializza i path SVG importati: il frame statico di
  `check_svg_group_isolation_gate.py` non è più trasparente.
- [x] Il control shape e i path SVG compaiono in `check_svg_cli_frame_gate.py`.
- [x] I frame endpoint di composite showcase differiscono e il repeat è
  byte-identico.
- [x] La motion gate passa tutti i 17 predicati, inclusi repeater e transform
  annidati.
- [x] Static composite e trim sampled mostrano rispettivamente il path e la
  crescita monotona attesa.
- [x] Gli otto gate raw di testo restano PASS e il loader resta 59/59.
- [x] Nessuna modifica a tolleranze, fixture o asserzioni dei gate.
- [x] CI same-SHA resta un gate esterno separato.

## Non obiettivi

- Parità browser completa o SMIL fuori dal contratto bounded.
- Certificazione Vulkan strict-native: il primo bersaglio è il backend software.
- Nuove feature SVG non richieste dai sei gate.

## Evidenza locale

Comando base:

```text
cmake --build build/chronon/linux-video-release --target chronon3d_cli -j4
CHRONON_CLI=$PWD/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli \
  python3 scripts/ci/check_svg_group_isolation_gate.py
```

Risultati finali del 2026-11-16: build exit 0; **sei gate path/motion PASS**
(group isolation 10/10, cli frame, composite 31 frame, motion 17/17, static
composite 132 px, trim `[0,0,67,96,96]` e repeat identico); otto gate di testo
PASS; `chronon3d_svg_path_loader_tests` 59/59 casi e 487/487 asserzioni;
`git diff --check` exit 0. La certificazione CI same-SHA non è stata eseguita e
resta esterna a questa scheda.
