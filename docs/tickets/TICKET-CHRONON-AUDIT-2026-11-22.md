# TICKET-CHRONON-AUDIT-2026-11-22 — Chronon hot path, correctness and cleanup

**Stato:** `ACTIVE` (2026-11-22)
**Origine:** audit statico del 2026-11-22 su Chronon3d, ChrononMotion3D,
ChrononPlugin e ChrononTemplate.
**Regola:** ogni voce ha un owner, un gate verificabile e uno stato esplicito.
Un fix non è `DONE` senza build e test sul commit che lo contiene.

## Inventario

| ID | Problema | Repo | Gravità | Stato | Owner |
|---|---|---|---|---|---|
| C3D-AUDIT-01 | Light-wrap: `download_surface` → blur CPU → `upload_surface` per frame | Chronon3d | P1 | OPEN | render graph |
| C3D-AUDIT-02 | LUT Curves/Lut3D: tabella + aux surface ricreate ogni frame | Chronon3d | P1 | OPEN | render graph |
| C3D-AUDIT-03 | Path animati disabilitano la `topology_cache` e rimaterializzano la scena | Chronon3d | P1 | OPEN | compiler/pipeline |
| C3D-AUDIT-04 | DoF attivo forza `graph_structure_unchanged=false` ogni frame | Chronon3d | P1 | OPEN | temporal pipeline |
| C3D-AUDIT-05 | Backdrop blur software O(radius) scalare, cache-unfriendly | Chronon3d | P2 | OPEN | software backend |
| C3D-AUDIT-06 | Fallback Vulkan GPU↔CPU + 74 `std::getenv` in hot path | Chronon3d | P1 | OPEN | Vulkan backend |
| C3D-AUDIT-07 | Decode immagini full-res prima del bound display | Chronon3d | P0 | OWNED | [`TICKET-PERF-ASSET-DECODE-HOTPATH`](TICKET-PERF-ASSET-DECODE-HOTPATH.md) |
| C3D-AUDIT-08 | `dirty_rects` non deterministico su ModernMap1 | Chronon3d | P0 | OWNED | [`TICKET-PERF-100X-VERIFIED-PLAN`](TICKET-PERF-100X-VERIFIED-PLAN.md) |
| C3D-AUDIT-09 | Lifetime GPU Vulkan/CUDA/NVENC e `VulkanBackend::Impl` monolitico | Chronon3d | P1 | OWNED | [`TICKET-MAINTENANCE-HOTSPOTS-BONIFICATION`](TICKET-MAINTENANCE-HOTSPOTS-BONIFICATION.md) |
| CM3D-AUDIT-01 | `MotionScene::evaluatePose`: `std::function` e scratch per layer | ChrononMotion3D | P1 | IN PROGRESS | [`TICKET-CM3D-EVAL-ALLOC-2026-11-22`](../../../ChrononMotion3D/docs/tickets/TICKET-CM3D-EVAL-ALLOC-2026-11-22.md) |
| CM3D-AUDIT-02 | Residuo 3D/PBR/stills e motion blur per-layer | ChrononMotion3D | P2 | OWNED | [`TICKET-CM3D-REMAINING-WORK-20261106`](../../../ChrononMotion3D/docs/tickets/TICKET-CM3D-REMAINING-WORK-20261106.md) |
| CPLG-AUDIT-01 | Map plate solo CPU, GeoJSON riparsato per job, encode via `popen` | ChrononPlugin | P1 | IN PROGRESS | [`TICKET-MAPS-PLATE-HOTPATH-2026-11-22`](../../../ChrononPlugin/docs/tickets/TICKET-MAPS-PLATE-HOTPATH-2026-11-22.md) |
| CT-AUDIT-01 | Monoliti `BackgroundPack`/`EditorialMapPack`/`WebShotsRebuildPack`/`MultiImagePack` | ChrononTemplate | P2 | OPEN | [`TICKET-CT-CLEANUP-2026-11-22`](../../../ChrononTemplate/docs/tickets/TICKET-CT-CLEANUP-2026-11-22.md) |
| REPO-AUDIT-01 | Log/PNG/`.obj`/cache generati nei repo | tutti | P3 | OPEN | [`TICKET-CT-CLEANUP-2026-11-22`](../../../ChrononTemplate/docs/tickets/TICKET-CT-CLEANUP-2026-11-22.md) |

## C3D-AUDIT-01 — light-wrap GPU → CPU → GPU

**Evidenza `[CODICE]`:** `src/render_graph/nodes/lighting_node.cpp:329-403`.
Dopo `lighting_surface_points`, se `image_light_wrap_asset` è attivo il nodo
scarica l'intera surface, alloca `rgba` + `tmp_colors`, esegue due box blur CPU
e ricarica la surface, per ogni frame.

**Fix:** portare il wrap su shader GPU e cachare il blurred wrap dell'asset
(content-addressed, non per frame). Il fallback CPU resta solo per backend che
non espongono il kernel.

**Gate:** MP4/overlay SHA invariato su un job con wrap; `download_surface`
count = 0 per frame; misura before/after di `gpu_execute_ms` e wall.

## C3D-AUDIT-02 — LUT aux ricreata ogni frame

**Evidenza `[CODICE]`:** `src/render_graph/nodes/effect_stack_node_native_bulk.cpp:782-887`.
`CompiledCurve`, `std::vector<float> table`, `bulk_upload_aux` e
`bulk_release_surface` vivono dentro il percorso per-frame.

**Fix:** cache della surface aux keyed da hash dei parametri Curves/Lut3D, con
budget e invalidazione esplicita; la tabella host va costruita una volta per
chiave, non una volta per frame.

**Gate:** dispatch LUT senza `create_surface`/`upload_surface` in steady state;
output pixel-identico su fixture Curves e Lut3D; memoria cache bounded.

## C3D-AUDIT-03 — topology cache e path animati

**Evidenza `[CODICE]`:** `src/runtime/render_pipeline.cpp:197-208` e
`src/compiler/scene_materializer.cpp:134`.

**Fix:** separare la scena base statica dai nodi path tempo-variabili; cachare
la base e aggiornare solo i path nel frame. Riusare la stessa identità di
`TICKET-PERF-100X-VERIFIED-PLAN` P2/P3.

**Gate:** `scene_materialize_count` in discesa sui frame con un solo path
animato; SHA identico; nessuna cache hit su topologia realmente cambiata.

## C3D-AUDIT-04 — DoF e ricostruzione del grafo

**Evidenza `[CODICE]`:** `src/render_graph/pipeline/temporal/scene_temporal_execution.cpp:203-208`.

**Fix:** mantenere `graph_structure_unchanged=true` quando cambiano solo i
parametri DoF; il grafo va ricostruito solo se cambia la topologia.

**Gate:** contatore rebuild grafo = 0 su un job DoF a topologia costante; SHA
invariato; wall before/after.

## C3D-AUDIT-05 — backdrop blur software

**Evidenza `[CODICE]`:** `src/render_graph/nodes/composite_backdrop_software.cpp:68-113`.

**Fix:** usare `DownsampleHalf`/`DownsampleQuarter` e i kernel SIMD già
presenti; il pass verticale deve leggere righe contigue, non colonne.

**Gate:** blur equivalente entro la tolleranza dello suite pixel; speedup
misurato; nessun accesso column-major nel loop verticale.

## C3D-AUDIT-06 — fallback Vulkan e getenv

**Evidenza `[CODICE]`:** `src/backends/vulkan/vulkan_native_shape_dispatch.cpp:152-300`
e 74 occorrenze di `std::getenv("CHRONON3D_*")` in `src/`.

**Fix:** leggere i flag una sola volta all'avvio (config immutabile per
processo) e sostituire i fallback GPU↔CPU per-frame con i kernel nativi o un
errore fail-closed.

**Gate:** zero `std::getenv` nei loop per-frame/per-nodo; test backend registry
e compositor; nessun fallback silenzioso nei job certificati.

## C3D-AUDIT-07/08/09 — voci già owned

Non si duplicano: il piano, le misure e i gate restano nei ticket linkati.
Questo ticket registra solo l'associazione audit → owner.

## Verifica di questa wave

- **Build/test: NOT RUN** — il 2026-11-22 la macchina aveva ~1 GiB free,
  2,7 GiB di swap usati e un `cargo check` già in corso. Nessun build è stato
  lanciato per non mandare l'host in OOM.
- Da eseguire appena la RAM torna disponibile:
  `cmake --build build/chronon/linux-video-test -j2`,
  `chronon3d_render_graph_tests`, `chronon3d_compositor_tests`,
  `chronon3d_backend_registry_tests`.
