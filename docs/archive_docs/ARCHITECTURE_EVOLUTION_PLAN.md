# Architecture Evolution Plan — Chronon3D

Stato presente: [`CURRENT_STATUS.md`](CURRENT_STATUS.md). Piano operativo:
[`ROADMAP.md`](ROADMAP.md).

## Frontiera corrente

`Composition → Scene → RenderGraph → FrameGraphCompiler → CompiledFrameGraph → GraphExecutor → RenderBackend → output`

Fondazioni completate:

- esecuzione solo su grafo compilato;
- rimozione overload raw graph e `ExecutionPlanCache`;
- scheduler esplicito;
- ID forti e hashing deterministico;
- stato per-sessione;
- `AssetResolver` tipizzato;
- registrazione esplicita tramite registry host-owned.

Restano aperti gate non affidabili, test scheduler obsoleti, `PrecompNode`, race sull’identità, scope annidati e confine SDK.

## Ownership canonica

- Core: contratti e invarianti.
- Feature: effetti, nodi, exporter, media e preset.
- Integration: registry, catalog, resolver, sampler ed extension point.
- Diagnostics: telemetria, profiling, debug e visual validation.
- Experimental: lavoro opt-in non esportato dallo SDK stabile.

Non creare registry, resolver, sampler, cache o execution path paralleli. Vedi [`ANTI_DUPLICATION_RULES.md`](ANTI_DUPLICATION_RULES.md).

## Registrazione

`ExtensionModule → ExtensionContext → CompositionRegistry / GraphNodeCatalog / EffectCatalog / AssetRegistry`

La registrazione statica globale è ritirata. Le composizioni cliente devono vivere in pack esterni.

## Target

```text
RenderRuntime: servizi engine-lifetime
RenderSession: stato job-owned
ExecutionScope: root/tile/precomp, parent, arena, identità
GraphExecutor: stateless, compiled graph, scheduler/scope espliciti
```

## Sequenza

1. Riparare gate e test.
2. Sistemare Precomp e lease.
3. Eliminare race e introdurre `ExecutionScope`.
4. Chiudere SDK/install consumer.
5. Riparare diagnostics/content e test fast.
6. Solo dopo riaprire performance avanzata e V3.

## Camera & Projection Pipeline

Decomposizione formale chiusa su `main@eb1ce8e5` (post C1–C7).  I tipi
canonici del projection contract sono ora POD disaccoppiati, ognuno
ospitato nella header del rispettivo dominio fisico, e propagati
end-to-end attraverso `Camera2_5D → EvaluatedProjection → renderer`.

- `struct FocalPx { f32 x; f32 y; }` in
  `include/chronon3d/math/camera_projection_contract.hpp` —
  prodotto da `camera_math::focal_xy_from_camera(...)` e consumato
  per-axis (X e Y indipendenti) da `CameraProjectionResolver`,
  `project_world_point`, e dal framing solver.
- `struct ViewportRect { f32 x; y; width; height; }` in
  `include/chronon3d/scene/model/camera/lens_model.hpp` —
  prodotto da `LensModel::effective_viewport(...)` con offset
  pillarbox/letterbox esplicito sotto `GateFit::Overscan`;
  consumato da `EvaluatedProjection::active_viewport` e dal
  principal point (centrato nel sub-rect attivo, non sul canvas
  grezzo).
- `inline FocalPx focal_xy_from_camera(...)` in
  `camera_projection_contract.hpp` — single source of truth per
  `focal_x_px` / `focal_y_px`.  Il legacy `focal_from_camera(...)`
  rimane come thin wrapper verso `.y` (additive, non rimuovibile
  durante il freeze).
- `LensModel::focal_xy_pixels(...)` applica `pixel_aspect` e
  `anamorphic_squeeze` SOLO sull'asse X (`lens_factor = pixel_aspect
  * anamorphic_squeeze`); l'asse Y è preservato.  Anamorfismo 2× in
  un viewport 16:9 produce quindi `focal_x / focal_y = 1.506 × 2.0
  = 3.011` (non 2.0 — il ratio dipende dall'aspect ratio del
  viewport contro il sensore).
- Golden test copre i 6 mode canonici in
  `tests/scene/camera/golden_projection_test.cpp` con tolleranza
  1e-3 e hash-free (strategia tolerance-only per stabilità
  cross-host FMA / fenv).

Nessun secondo solver ottico, registry, resolver o sampler è ammesso
in V3, V4 o backend aggiuntivi: qualunque nuova pipeline deve
riusare `FocalPx` / `ViewportRect` / `focal_xy_from_camera` /
`LensModel::effective_viewport`.

## Mesh V1 — Fase 6

**Stato: design pianificato, non ancora implementato.** Il codice contiene già parzialmente `ShapeType::Mesh`, `MeshShape` e il processor software; `AssetKind::Mesh`, `MeshRef`, `MeshLoader` e `Prepared MeshSource` restano da introdurre.

Mesh V1 tratta la mesh come contenuto di un `Layer`, non come una seconda architettura di scena. Il perimetro V1 è **solo `.glb` self-contained**: `.gltf` e dipendenze esterne (`.bin`, immagini esterne, ecc.) sono fuori scope e potranno essere valutati in V1.1.

### Confine authoring/runtime

`LayerBuilder::mesh()` è deliberatamente stupido: conserva soltanto un `MeshRef`/logical asset reference nel `Layer` e dichiara la dipendenza nell’`AssetManifest`. Non deve accedere al filesystem, montare o interrogare `AssetResolver`, parsare GLB, allocare una cache o caricare geometria. Il builder lavora esclusivamente con path logici, come gli altri asset authoring.

Il percorso canonico è:

```text
layer.mesh(asset("models/phone.glb"))  # notazione concettuale
        ↓
     MeshRef  # planned V1 logical reference
        ↓
Layer / Composition definition
        ↓
  AssetManifest
        ↓
  prepare_render()
        ↓
  AssetResolver
        ↓
    MeshLoader
        ↓
 Prepared MeshSource
        ↓
   RenderGraph
        ↓
  ShapeType::Mesh
        ↓
 RenderBackend
```

La risoluzione del filesystem appartiene esclusivamente all’`AssetResolver` posseduto da `RenderRuntime`. Il caricamento e l’import GLB avvengono una volta alla preparation/compile boundary; il frame loop riceve soltanto geometria preparata. Non introdurre un nuovo `Model`, `Entity`, ECS, `Transform3D`, `MeshAnimation`, `MeshCamera`, runtime scene graph, `ModelManager`, resolver o registry parallelo.

### Contratti V1 pianificati

- **Asset surface (planned):** introdurre `AssetKind::Mesh` e `MeshRef`, riusando `AssetManifest`/`AssetResolver` canonici.
- **Authoring (planned):** `LayerBuilder::mesh(MeshRef)` conserva il riferimento logico e la dependency; nessun I/O o parsing nel builder.
- **Preparation (planned):** `AssetResolver → MeshLoader → Prepared MeshSource`. La preparation boundary deve eseguire il caricamento/import una sola volta prima del frame loop e riusare, quando disponibile, una cache canonica per asset identity; una nuova cache dedicata richiede prima la decisione architetturale prevista dalle regole del repository.
- **Import (planned):** il loader confina i tipi glTF al proprio modulo; POSITION, NORMAL, UV, indici e materiale base diventano tipi Chronon preparati. Le immagini embedded del GLB restano subresource preparate, non path logici inventati.
- **Transform (planned):** il bake delle node transform avviene all’import; vertici, normali, bounds, winding/orientamento e conversione glTF→Chronon devono essere coerenti. Transform e animazione del `Layer` restano fuori da `MeshSource` e riusano le API esistenti (`position`, `scale`, `rotate`, `rotate_anim`, camera 2.5D).
- **Render (planned):** usare `ShapeType::Mesh`, `MeshShape`, il `RenderGraph` e lo snapshot dei processor esistenti; non creare un graph node o una pipeline di dispatch separata.
- **Backend (acceptance criterion):** il gate principale è un cubo ruotato con perspective e depth corretti, includendo front/side/top visibili. La rasterizzazione filled/depth deve essere certificata, non assunta dalla presenza del renderer wireframe.
- **Failure/build (acceptance criteria):** asset mancante o GLB non valido deve fallire loud alla preparation; `CHRONON3D_ENABLE_MESH=OFF` deve compilare senza richiedere la superficie Mesh o il relativo import support. Questi comportamenti non sono ancora certificati dall’implementazione corrente.

### Definition of Done Fase 6

Triangolo GLB visibile, cubo GLB texturizzato, coordinate XYZ, perspective, depth ordering, trasformazione/animazione già esistente del `Layer`, preflight dell’asset mancante, determinismo random-access e build `CHRONON3D_ENABLE_MESH=OFF`. Nessun caricamento per-frame e nessuna dipendenza `.gltf` esterna in V1.

## V3 tile-first

V3 è futuro lavoro di sostituzione. P1–P10 restano pianificati. Ogni componente deve dichiarare il percorso V2 sostituito, test di equivalenza, criterio di rimozione e milestone di eliminazione.

## Confine consumer

Un consumer esterno deve includere solo header pubblici, collegare solo `Chronon3D::SDK`, evitare `src/` e `chronon3d_experimental/`, e ricevere servizi tramite contratti espliciti.
