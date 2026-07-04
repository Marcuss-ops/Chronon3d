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

## Expressions V2

Percorso reale: `experimental/expressions/`.
Header: `experimental/expressions/include/chronon3d_experimental/expressions/v2/`.
Build gate: `CHRONON3D_BUILD_EXPERIMENTAL`, default OFF.
Non è installato, esportato o usato dal percorso produttivo.

TICKET-003 e TICKET-004 sono chiusi. Il prossimo lavoro è TICKET-EXP2-G3: migrare Path A verso Path B senza due parser/VM produttivi. La promozione richiede tutti gli otto gate di [`EXPRESSIONS_V2_PROMOTION.md`](EXPRESSIONS_V2_PROMOTION.md).

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

## V3 tile-first

V3 è futuro lavoro di sostituzione. P1–P10 restano pianificati. Ogni componente deve dichiarare il percorso V2 sostituito, test di equivalenza, criterio di rimozione e milestone di eliminazione.

## Confine consumer

Un consumer esterno deve includere solo header pubblici, collegare solo `Chronon3D::SDK`, evitare `src/` e `chronon3d_experimental/`, e ricevere servizi tramite contratti espliciti.
