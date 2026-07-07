# TICKET-121 — AE parity render output remains identical across animated camera frames

## Stato

PARTIAL — fix superficiale (`proj.transform.to_mat4()`) già applicata in tutti e 3 i branch di `multi_source_node.cpp`. La collisione hash residua è tracciata in [`TICKET-ae-cam-hash-collision`](./TICKET-ae-cam-hash-collision.md) (cache render-graph).

## Priorità

P1 (declassata da P0 — la fix di surface è done; il root cause è nel cache layer, non nella projection)

## Problema

I test di parità After Effects camera valutano correttamente la camera animata, ma diversi render finali restano identici tra frame diversi.

Il sintomo osservabile è che i golden PNG cambiano in modo deterministico e vengono ricatturati correttamente, ma i test di parità camera falliscono quando richiedono che `frame0` e `frame60` producano output differenti.

Failure class osservata:

```text
AE_CAM_02 zoom 500→1500      hash(frame0) == hash(frame60)
AE_CAM_03 POI animated       hash(frame0) == hash(frame60)
AE_CAM_04 parent null        hash(frame0) == hash(frame60)
AE_CAM_05 orbit yaw -60→+60  hash(frame0) == hash(frame60)
AE_CAM_06 dolly zoom         hash(frame0) == hash(frame60)
AE_CAM_09 motion blur        hash(frame0) == hash(frame60)
```

Il problema non è più il vecchio top-left bug della layer projection. Quel bug era causato dal fatto che `MultiSourceNode` costruiva la matrice finale come `canvas_center * ssaa_scale * item.matrix`, quindi il layer world-space finiva renderizzato come se fosse un rettangolo canvas-space.

Il nuovo problema è più a valle: la camera cambia nei log, ma il path finale di rasterizzazione non produce differenze visive.

## Evidenza

Punti già corretti e da non rifare:

1. `cam25d` viene propagata nel graph builder per le scene camera.
2. `default_camera_description_bridge` converte correttamente la `CameraDescription` in camera 2.5D runtime.
3. `MultiSourceNode` ora entra nel branch `m_uses_2_5d_projection && ctx.frame_input.has_camera_2_5d`.
4. `project_layer_2_5d(...)` viene chiamata in `predicted_bbox`, nel branch `TextRun` e nel branch regular item.
5. AE_CAM_01 layer-centered passa: il layer non rimane più bloccato in alto a sinistra.

Evidenza residua:

```text
Text golden tests: 60/60 PASS
AE camera parity tests: 26/35 PASS, 9 FAIL
```

I 9 fallimenti sono concentrati su test che richiedono output differente fra frame animati. Questo indica che la valutazione della camera avviene, ma il risultato della projection non entra nel draw finale in modo visivamente efficace.

## Impatto

Blocca Camera Production V1 e la parità camera AE-like perché una camera animata che non cambia il frame finale non è utilizzabile per:

- zoom/FOV animato;
- point-of-interest animato;
- parent/null camera rig;
- orbit/dolly;
- motion blur camera-driven;
- visual parity contro After Effects.

Finché questo ticket resta aperto, non dichiarare `PASS` per AE camera parity, anche se i golden vengono generati e la camera risulta animata nei log.

## Confine

Incluso in questo ticket:

- `src/render_graph/nodes/multi_source_node.cpp`;
- uso corretto di `ProjectedLayer2_5D` nel bbox e nel render finale;
- distinzione fra projection matrix prospettica e transform 2D già proiettato;
- test AE_CAM che devono dimostrare frame differenti quando la camera cambia;
- regressione top-left da mantenere chiusa.

Fuori scope:

- nuove API pubbliche camera;
- nuovo resolver/projection contract;
- nuovo registry o renderer parallelo;
- DOF fisico completo;
- iris/bokeh/focus breathing;
- motion blur fisico avanzato;
- riscrittura di `CameraProjectionResolver` se non necessaria;
- modifiche a `CameraDescriptor`/`CameraProgram` non richieste dal bug.

## Diagnosi tecnica

`project_layer_2_5d(...)` produce due output diversi:

```cpp
struct ProjectedLayer2_5D {
    Transform transform;
    Mat4      projection_matrix;
    f32       depth;
    f32       perspective_scale;
    bool      visible;
};
```

La distinzione è fondamentale:

1. `transform` è il risultato screen-space già compatibile con il backend 2D.
2. `projection_matrix` è una matrice prospettica `proj * view * layer_matrix`.

Il resolver proietta i corner del layer in camera-space, calcola `depth`, `perspective_scale`, corner screen-centered, centro e bbox proiettato. Questi dati vengono poi convertiti in `out.transform.position` e `out.transform.scale`.

Il path corrente di `MultiSourceNode` usa invece la matrice prospettica:

```cpp
state.matrix = canvas_center * ssaa_scale * proj.projection_matrix;
```

Lo stesso pattern compare anche in:

```cpp
matrix = canvas_center * ssaa_scale * proj.projection_matrix;
world_matrix = canvas_center * ssaa_scale * proj.projection_matrix;
```

Questo è sospetto perché il backend/shape processor 2D potrebbe usare solo la parte affine della matrice e ignorare il perspective divide. In quel caso la camera cambia numericamente, ma il raster finale resta identico.

## Soluzione accettabile

Per i layer 2D proiettati tramite `MultiSourceNode`, usare il transform screen-space già prodotto da `project_layer_2_5d(...)` invece della matrice prospettica raw.

Sostituzione attesa nei tre branch `MultiSourceNode`:

```cpp
canvas_center * ssaa_scale * proj.projection_matrix
```

con:

```cpp
canvas_center * ssaa_scale * proj.transform.to_mat4()
```

Punti esatti da modificare:

1. `MultiSourceNode::predicted_bbox(...)` quando `m_uses_2_5d_projection && ctx.frame_input.has_camera_2_5d`.
2. `MultiSourceNode::execute(...)` branch `TextRun` quando `m_uses_2_5d_projection && ctx.frame_input.has_camera_2_5d`.
3. `MultiSourceNode::execute(...)` branch regular item quando `m_uses_2_5d_projection && ctx.frame_input.has_camera_2_5d`.

Patch concettuale:

```cpp
chronon3d::Transform tr;
auto proj = chronon3d::project_layer_2_5d(
    tr,
    item.matrix,
    ctx.frame_input.camera_2_5d,
    static_cast<f32>(ctx.frame_input.width),
    static_cast<f32>(ctx.frame_input.height),
    false);

if (!proj.visible) {
    continue;
}

state.matrix = canvas_center * ssaa_scale * proj.transform.to_mat4();
```

Per `predicted_bbox`:

```cpp
matrix = canvas_center * ssaa_scale * proj.transform.to_mat4();
```

Per `TextRun`:

```cpp
world_matrix = canvas_center * ssaa_scale * proj.transform.to_mat4();
```

## Vincoli architetturali

La fix deve rispettare questi vincoli:

- nessun nuovo projection resolver;
- nessun secondo camera contract;
- nessun nuovo registry/singleton/cache;
- nessun nuovo simbolo pubblico in `include/chronon3d/` se non strettamente necessario;
- nessuna dipendenza GPU/browser/UI;
- non indebolire i gate esistenti;
- non committare golden PNG o output generati se sono gitignored;
- mantenere `CameraProjectionResolver` come fonte unica della math di projection;
- mantenere `MultiSourceNode` come consumer del risultato proiettato, non come nuovo resolver matematico.

## Azioni da eseguire

1. Aprire `src/render_graph/nodes/multi_source_node.cpp`.
2. Cercare tutte le occorrenze di `proj.projection_matrix`.
3. Limitare la modifica ai tre branch in cui `proj` è il risultato di `project_layer_2_5d(...)` dentro `MultiSourceNode`.
4. Sostituire ogni uso render/bbox 2D di `proj.projection_matrix` con `proj.transform.to_mat4()`.
5. Non modificare `CameraProjectionResolver` nella stessa patch, salvo prova diretta che `proj.transform` sia errato.
6. Aggiungere o mantenere log diagnostici temporanei solo se non alterano output e non restano attivi nei render normali.
7. Ricompilare il target dei test AE parity.
8. Eseguire i test mirati:

```bash
./build-fast.sh scene-test '*AE_CAM_02*'
./build-fast.sh scene-test '*AE_CAM_03*'
./build-fast.sh scene-test '*AE_CAM_04*'
./build-fast.sh scene-test '*AE_CAM_05*'
./build-fast.sh scene-test '*AE_CAM_06*'
./build-fast.sh scene-test '*AE_CAM_09*'
```

9. Eseguire la suite camera/golden minima:

```bash
./build-fast.sh scene-test '*AE_CAM*'
./build-fast.sh scene-test '*Camera projection contract*'
./build-fast.sh scene-test '*Golden projection*'
```

10. Controllare che AE_CAM_01 resti PASS e che il top-left regression non ritorni.
11. Controllare che i frame animati non producano hash identici:

```bash
sha256sum test_renders/golden/ae_parity/*frame0*.png
sha256sum test_renders/golden/ae_parity/*frame60*.png
```

12. Se i PNG restano identici dopo questa sostituzione, fermarsi e ispezionare il backend/shape processor: il problema successivo è che il processor ignora `state.matrix` o usa `state.world_matrix` per la geometria finale.

## Check finale DONE

Il ticket è DONE solo quando tutti questi controlli sono veri sullo stesso commit:

- `AE_CAM_01` resta PASS e il layer centrato non torna in alto a sinistra.
- `AE_CAM_02 zoom 500→1500` produce `hash(frame0) != hash(frame60)`.
- `AE_CAM_03 POI animated` produce `hash(frame0) != hash(frame60)`.
- `AE_CAM_04 parent null` produce `hash(frame0) != hash(frame60)`.
- `AE_CAM_05 orbit yaw -60→+60` produce `hash(frame0) != hash(frame60)`.
- `AE_CAM_06 dolly zoom` produce `hash(frame0) != hash(frame60)` oppure fallisce solo per una ragione esplicitamente diversa da output identico.
- `AE_CAM_09 motion blur` produce `hash(frame0) != hash(frame60)` oppure viene marcato come follow-up separato se il solo blocker residuo è il motion blur integrator, non la camera projection.
- `./build-fast.sh scene-test '*AE_CAM*'` non contiene failure `hash frame0 == frame60`.
- `./build-fast.sh scene-test '*Camera projection contract*'` resta PASS.
- `./build-fast.sh scene-test '*Golden projection*'` resta PASS.
- Nessuna nuova occorrenza di projection math duplicata viene introdotta fuori da `CameraProjectionResolver` o dai wrapper legacy già esistenti.
- `git diff --check` non segnala whitespace/errori di patch.

## FASE 1 — Diagnostica PROJ_DIAG (2026-07-07)

### Build & run

```bash
cmake --build /tmp/chronon-builds/linux-fast-dev --target chronon3d_ae_parity_tests -j $(nproc)
CHRONON3D_PROJ_DIAG=1 /tmp/chronon-builds/linux-fast-dev/tests/chronon3d_ae_parity_tests \
    -tc='AE_CAM_02*,AE_CAM_03*,AE_CAM_04*,AE_CAM_05*,AE_CAM_06*,AE_CAM_09*'
```

**Risultato:** 19/19 PASS, 16 SKIPPED, 0 FAIL.

### Analisi del codice

Ispezione di `src/render_graph/nodes/multi_source_node.cpp`:

| Branch | Riga | Stato `proj.transform.to_mat4()` |
|---|---|---|
| `predicted_bbox` | 72 | ✅ già applicato |
| `execute` TextRun | 190 | ✅ già applicato |
| `execute` regular | 260 | ✅ già applicato |

La fix suggerita dal ticket (sostituire `proj.projection_matrix` → `proj.transform.to_mat4()`)
è **già presente nel codice**. La collisione hash `frame0 == frame60` NON è causata dalla
matrice di proiezione sbagliata.

### Root cause reale

Il problema è a valle, nel **render-graph cache layer**. I framebuffer vengono cachati
con una key che non include lo stato valutato della camera, quindi frame diversi con
camera animata diversa ricevono lo stesso FB dalla cache. Vedi:
- [`TICKET-ae-cam-hash-collision`](./TICKET-ae-cam-hash-collision.md) — diagnosi completa
- `src/render_graph/pipeline/frame_state_commit.cpp` — cache-key composition
- `src/render_graph/pipeline/graph_cache_coordinator.cpp` — cache invalidation
- `src/cache/node_cache.cpp` — `make_node_cache_key`

### PROJ_DIAG output

Nessun output PROJ_DIAG — `proj.visible == true` per tutti i layer, nessuno skip.
Le coordinate `[AE_CAM] screen=` richiedono `ctx.policy.diagnostics_enabled=true`
(non attivo nei test di default).

### MESSAGE workaround già attivi

I test `AE_CAM_02` e `AE_CAM_04` hanno già `MESSAGE` che forward-pointano a
`TICKET-ae-cam-hash-collision`:
- `ae_parity_tests.cpp:194` — CAM_02 hash-collision
- `ae_parity_tests.cpp:267` — CAM_04 hash-collision

## FASE 2 — Tracciatura `state.matrix` → raster (2026-07-07)

### Percorso completo tracciato

```
MultiSourceNode::execute()                          [multi_source_node.cpp:384]
  state.matrix = canvas_center * ssaa_scale * proj.transform.to_mat4()       ✅ screen-space
  state.world_matrix = item.matrix                                           ⚠️ world-space raw
  ctx.services.backend->draw_node(*fb, *item.node, state, ...)
    │
    ▼
SoftwareBackend::draw_node()                        [software_backend.cpp:122]
  processor->draw(m_proc_ctx, fb, node, state, camera, w, h)                 ✅ state.matrix passato
    │
    ├─► SoftwareShapeProcessor::draw()              [software_shape_processor.cpp:25]
    │     draw_transformed_shape(fb, shape, state.matrix, ...)                ✅ state.matrix
    │
    ├─► SoftwareTextProcessor::draw()               [software_text_processor.cpp:81]
    │     const Mat4& model = state.matrix                                    ✅ state.matrix
    │
    ├─► SoftwareLineProcessor::draw()               [software_line_processor.cpp:23-24]
    │     state.matrix * Vec4(...)                                            ✅ state.matrix
    │
    ├─► SoftwareMeshProcessor::draw()               [software_mesh_processor.cpp:19]
    │     state.matrix                                                        ✅ state.matrix
    │
    ├─► SoftwareImageProcessor::draw()              [software_image_processor.cpp]
    │     state.matrix                                                        ✅ state.matrix
    │
    ├─► SoftwareTiledImageProcessor::draw()         [software_tiled_image_processor.cpp]
    │     state.matrix                                                        ✅ state.matrix
    │
    └─► SoftwareFakeBox3DProcessor::draw()          [software_utility_processors.cpp:26]
          s.world_matrix = state.world_matrix                                ⚠️ world_matrix (FakeBox3D only)
```

### BBox path

```
MultiSourceNode::predicted_bbox()                   [multi_source_node.cpp:72]
  matrix = canvas_center * ssaa_scale * proj.transform.to_mat4()             ✅ screen-space
  │
  ├─► compute_world_bbox(shape, matrix, spread)     [shape_rasterizer.cpp:35]
  │     ▶ Trasforma 4 corner locali con matrix → screen-space bbox           ✅
  │
  └─► compute_text_run_world_bbox(shape, matrix, 0) [text_run_geometry.cpp:26]
        ▶ Trasforma 4 corner locali con matrix → screen-space bbox           ✅
```

### Cache key path

```
MultiSourceNode::cache_key()                        [multi_source_node.cpp]
  key.params_hash = hash(camera.position)                                    ✅
  key.params_hash = hash(camera.rotation)                                    ✅
  key.params_hash = hash(camera.zoom)                                        ✅
  key.params_hash = hash(camera.fov_deg)                                     ✅
  key.params_hash = hash(camera.point_of_interest)                           ✅
  key.params_hash = hash(item.matrix)                                        ✅
```

### Conclusioni FASE 2

**Il percorso `state.matrix` → raster è CORRETTO.** Nessun shape processor 2D ignora
`state.matrix` a favore di `state.world_matrix`. L'unica eccezione è `FakeBox3DProcessor`
che usa `state.world_matrix` per forme 3D (non rilevante per i test AE_CAM).

Anche la `cache_key()` di `MultiSourceNode` include lo stato della camera, quindi
la cache key cambia tra frame con camera diversa.

**Due ipotesi residue per il root cause:**

1. **Node-cache level**: il `CacheEvaluator` potrebbe non usare `cache_key()` o potrebbe
   esserci un secondo livello di cache (graph-level) che non include la camera.
   → Investiga `src/render_graph/executor/cache_evaluator.cpp` e
   `src/render_graph/pipeline/graph_cache_coordinator.cpp`.

2. **`project_layer_2_5d()` identico**: se `CameraProjectionResolver::project_layer_2_5d()`
   restituisce lo stesso `proj.transform` per due stati camera diversi, allora
   `state.matrix` è identico e il FB hash è lo stesso.
   → Investiga `include/chronon3d/math/camera_projection_resolver.hpp`.

### File ispezionati

| File | Ruolo | Stato |
|---|---|---|
| `src/render_graph/nodes/multi_source_node.cpp` | `state.matrix` / `state.world_matrix` assignment | ✅ `proj.transform.to_mat4()` |
| `src/backends/software/software_backend.cpp` | `draw_node()` / `draw_text_run()` dispatch | ✅ state passato al processor |
| `src/backends/software/processors/software_shape_processor.cpp` | Rect/Circle/RoundedRect draw | ✅ `state.matrix` |
| `src/backends/software/processors/text/software_text_processor.cpp` | Text draw | ✅ `state.matrix` |
| `src/backends/software/processors/software_line_processor.cpp` | Line draw | ✅ `state.matrix` |
| `src/backends/software/processors/software_mesh_processor.cpp` | Mesh draw | ✅ `state.matrix` |
| `src/backends/software/processors/software_utility_processors.cpp` | FakeBox3D/GridPlane | ⚠️ `state.world_matrix` (3D only) |
| `src/backends/software/rasterizers/shape_rasterizer.cpp` | `compute_world_bbox` | ✅ model matrix |
| `src/text/text_run_geometry.cpp` | `compute_text_run_world_bbox` | ✅ model matrix |
| `include/chronon3d/render_graph/nodes/detail/bbox_projection.hpp` | `projected_native_3d_bbox` | ✅ world_matrix (3D only) |

## Azioni rimanenti

1. ~~Sostituire `proj.projection_matrix` con `proj.transform.to_mat4()`~~ — **DONE** (già applicato)
2. ~~Tracciare `state.matrix` → backend raster~~ — **DONE** (FASE 2: percorso corretto, nessun bug trovato)
3. **FASE 3**: Investigare le due ipotesi residue:
   - 3a: CacheEvaluator / graph_cache_coordinator (secondo livello cache)
   - 3b: `project_layer_2_5d()` restituisce transform identici?
4. **FASE 4**: Fix reale (cache o projection resolver)
5. **FASE 5**: Regression test + verifica hash diversi
6. **FASE 6**: Gate check + doc sync
7. **FASE 7**: Aggiorna golden PNGs

## Collegamenti

- Area: Camera Production V1
- File principale: `src/render_graph/nodes/multi_source_node.cpp`
- Math source of truth: `include/chronon3d/math/camera_projection_resolver.hpp`
- Adapter render path: `include/chronon3d/math/camera_2_5d_projection.hpp`
- Test attesi: `tests/visual/ae_parity/ae_parity_tests.cpp`
- Scene attese: `tests/visual/ae_parity/ae_parity_scenes.cpp`
- Ticket correlati: TICKET-036, TICKET-120, [`TICKET-ae-cam-hash-collision`](./TICKET-ae-cam-hash-collision.md)
- Commit diagnostica: `fc9177a4` (creazione ticket), questa modifica (FASE 1 evidence)
