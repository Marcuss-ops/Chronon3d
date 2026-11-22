# TICKET-CHRONON-AUDIT-2026-11-22 — Chronon hot path, correctness and cleanup

**Stato:** `ACTIVE` (2026-11-22)
**Origine:** audit statico del 2026-11-22 su Chronon3d, ChrononMotion3D,
ChrononPlugin e ChrononTemplate.
**Regola:** ogni voce ha un owner, un gate verificabile e uno stato esplicito.
Un fix non è `DONE` senza build e test sul commit che lo contiene.

## Inventario

| ID | Problema | Repo | Gravità | Stato | Owner |
|---|---|---|---|---|---|
| C3D-AUDIT-01 | Light-wrap: `download_surface` → blur CPU → `upload_surface` per frame | Chronon3d | P1 | OPEN | [TICKET-C3D-LIGHTWRAP-GPU-2026-11-22](TICKET-C3D-LIGHTWRAP-GPU-2026-11-22.md) |
| C3D-AUDIT-02 | LUT Curves/Lut3D: tabella + aux surface ricreate ogni frame | Chronon3d | P1 | DONE (measured) | [TICKET-C3D-LUT-STEADYSTATE-VERIFY-2026-11-22](TICKET-C3D-LUT-STEADYSTATE-VERIFY-2026-11-22.md) |
| C3D-AUDIT-03 | Path animati disabilitano la `topology_cache` e rimaterializzano la scena | Chronon3d | P1 | OPEN | [TICKET-C3D-TOPOLOGY-ANIMATED-PATHS-2026-11-22](TICKET-C3D-TOPOLOGY-ANIMATED-PATHS-2026-11-22.md) |
| C3D-AUDIT-04 | DoF attivo forza `graph_structure_unchanged=false` ogni frame | Chronon3d | P1 | OPEN | [TICKET-C3D-DOF-STRUCTURE-FLAG-2026-11-22](TICKET-C3D-DOF-STRUCTURE-FLAG-2026-11-22.md) |
| C3D-AUDIT-05 | Backdrop blur software O(radius) scalare, cache-unfriendly | Chronon3d | P2 | DONE | software backend |
| C3D-AUDIT-06 | Fallback Vulkan GPU↔CPU + 74 `std::getenv` in hot path | Chronon3d | P1 | IN PROGRESS | [TICKET-C3D-VULKAN-FALLBACK-FAILCLOSED-2026-11-22](TICKET-C3D-VULKAN-FALLBACK-FAILCLOSED-2026-11-22.md) |
| C3D-AUDIT-07 | Decode immagini full-res prima del bound display | Chronon3d | P0 | OWNED | [`TICKET-PERF-ASSET-DECODE-HOTPATH`](TICKET-PERF-ASSET-DECODE-HOTPATH.md) |
| C3D-AUDIT-08 | `dirty_rects` non deterministico su ModernMap1 | Chronon3d | P0 | OWNED | [`TICKET-PERF-100X-VERIFIED-PLAN`](TICKET-PERF-100X-VERIFIED-PLAN.md) |
| C3D-AUDIT-09 | Lifetime GPU Vulkan/CUDA/NVENC e `VulkanBackend::Impl` monolitico | Chronon3d | P1 | OWNED | [`TICKET-MAINTENANCE-HOTSPOTS-BONIFICATION`](TICKET-MAINTENANCE-HOTSPOTS-BONIFICATION.md) |
| CM3D-AUDIT-01 | `MotionScene::evaluatePose`: `std::function` e scratch per layer | ChrononMotion3D | P1 | IN PROGRESS | [`TICKET-CM3D-EVAL-ALLOC-2026-11-22`](../../../ChrononMotion3D/docs/tickets/TICKET-CM3D-EVAL-ALLOC-2026-11-22.md) |
| CM3D-AUDIT-02 | Residuo 3D/PBR/stills e motion blur per-layer | ChrononMotion3D | P2 | OWNED | [`TICKET-CM3D-REMAINING-WORK-20261106`](../../../ChrononMotion3D/docs/tickets/TICKET-CM3D-REMAINING-WORK-20261106.md) |
| CPLG-AUDIT-01 | Map plate solo CPU, GeoJSON riparsato per job, encode via `popen` | ChrononPlugin | P1 | IN PROGRESS | [HOTPATH](../../../ChrononPlugin/docs/tickets/TICKET-MAPS-PLATE-HOTPATH-2026-11-22.md) / [NVENC-SHA](../../../ChrononPlugin/docs/tickets/TICKET-MAPS-NVENC-SHA-GATE-2026-11-22.md) |
| CT-AUDIT-01 | Monoliti `BackgroundPack`/`EditorialMapPack`/`WebShotsRebuildPack`/`MultiImagePack` | ChrononTemplate | P2 | OPEN | [TICKET-CT-PACKS-HELPER-DEDUP-2026-11-22](../../../ChrononTemplate/docs/tickets/TICKET-CT-PACKS-HELPER-DEDUP-2026-11-22.md) |
| REPO-AUDIT-01 | Log/PNG/`.obj`/cache generati nei repo | tutti | P3 | OPEN | [TICKET-REPO-ARTIFACTS-HYGIENE-2026-11-22](../../../ChrononTemplate/docs/tickets/TICKET-REPO-ARTIFACTS-HYGIENE-2026-11-22.md) |

## Ticket aperti da questa wave

- [TICKET-C3D-LIGHTWRAP-GPU-2026-11-22](TICKET-C3D-LIGHTWRAP-GPU-2026-11-22.md) — C3D-AUDIT-01
- [TICKET-C3D-LUT-STEADYSTATE-VERIFY-2026-11-22](TICKET-C3D-LUT-STEADYSTATE-VERIFY-2026-11-22.md) — DONE (measured), C3D-AUDIT-02
- [TICKET-C3D-TOPOLOGY-ANIMATED-PATHS-2026-11-22](TICKET-C3D-TOPOLOGY-ANIMATED-PATHS-2026-11-22.md) — C3D-AUDIT-03
- [TICKET-C3D-DOF-STRUCTURE-FLAG-2026-11-22](TICKET-C3D-DOF-STRUCTURE-FLAG-2026-11-22.md) — C3D-AUDIT-04
- [TICKET-C3D-VULKAN-FALLBACK-FAILCLOSED-2026-11-22](TICKET-C3D-VULKAN-FALLBACK-FAILCLOSED-2026-11-22.md) — parte residua C3D-AUDIT-06
- [TICKET-C3D-TEXT-FIXTURES-HERMETIC-2026-11-22](TICKET-C3D-TEXT-FIXTURES-HERMETIC-2026-11-22.md) — DONE, suite render-graph 469/469
- [TICKET-CM3D-BOUNDARY-ARTIFACTS-2026-11-22](../../../ChrononMotion3D/docs/tickets/TICKET-CM3D-BOUNDARY-ARTIFACTS-2026-11-22.md) — boundary CTest rosso
- [TICKET-CM3D-FULL-SUITE-BUILD-2026-11-22](../../../ChrononMotion3D/docs/tickets/TICKET-CM3D-FULL-SUITE-BUILD-2026-11-22.md) — 70 test Motion da costruire/eseguire
- [TICKET-MAPS-NVENC-SHA-GATE-2026-11-22](../../../ChrononPlugin/docs/tickets/TICKET-MAPS-NVENC-SHA-GATE-2026-11-22.md) — gate NVENC/SHA residuo
- [TICKET-CT-PACKS-HELPER-DEDUP-2026-11-22](../../../ChrononTemplate/docs/tickets/TICKET-CT-PACKS-HELPER-DEDUP-2026-11-22.md) — CT-AUDIT-01
- [TICKET-REPO-ARTIFACTS-HYGIENE-2026-11-22](../../../ChrononTemplate/docs/tickets/TICKET-REPO-ARTIFACTS-HYGIENE-2026-11-22.md) — REPO-AUDIT-01

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

**Chiusura 2026-11-22:** `tests/render_graph/nodes/test_effect_stack_lut_cache.cpp`
(2/2, 56 asserzioni) misura zero create/upload al secondo frame, hit +1,
miss su cambio tabella e hit al ripristino, senza release dell'aux cachata.
Parità Curves/Lut3D 16/16, max abs error `5.96e-08`; suite render-graph
469/469. Stato: **DONE (measured)**.

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

## Verifica di questa wave (2026-11-22)

- **RAM/build safety:** host 29 GiB; available 16-19 GiB durante i build.
  Nessun OOM. Build Chronon3d a `-j1`/`-j3`, Motion a `-j2`, Plugin `-j1`.
- **Build Chronon3d:** `chronon3d_graph_nodes`, `chronon3d_graph_executor`,
  `chronon3d_backend_vulkan` compilati e linkati senza errori.
- **`chronon3d_compositor_tests`:** PASS — 71/71 test, 651082 asserzioni.
- **`chronon3d_deterministic_tests`:** PASS — 94/94 test, 11797 asserzioni.
- **`chronon3d_backend_registry_tests`:** caso Curves/Lut3D PASS (16/16,
  max abs error 5.96e-08). Suite completa: 54 PASS / 107 FAIL, tutti i FAIL
  sono `vkCreateInstance failed with VkResult=-9` (driver Vulkan assente o
  incompatibile); 1906547 asserzioni eseguite passano.
- **`chronon3d_render_graph_tests`:** **469/469 PASS**, 19901/19901
  asserzioni, 0 skipped. Nessun `Missing glyph` / shaping fallita nel log.
  I 2 nuovi casi LUT misurano la cache; il caso watermark ora passa dopo il
  fix del downscale immagini.
- **Fix immagine collaterale:** `downscale_for_runtime` impostava le nuove
  dimensioni ma faceva `return` prima di spostare i buffer ridimensionati;
  il framebuffer risultava quindi con dimensioni nuove e pixel vecchi. Il
  watermark OVERLAY-CERT-1 passa da 840 a >2000 pixel accesi e il test
  10/10 è verde.

### C3D-AUDIT-02 — evidenza

- `bulk_cached_lut_aux` usa `GpuAssetCache` (budget default 512 MB) con chiave
  SHA-256 della tabella + formato/dimensioni; la surface aux viene rilasciata
  solo quando `cache_owned == false`.
- Parità pixel Curves/Lut3D verificata dal test backend sopra.
- Conteggio diretto strumentato: `test_effect_stack_lut_cache.cpp` (2/2,
  56 asserzioni) misura 1 upload al primo frame, 0 al secondo, hit +1,
  nuovo upload su cambio tabella e hit al ripristino.
- Evidenza cache completa: suite `chronon3d_render_graph_tests` 469/469.

### C3D-AUDIT-05 — evidenza

- Passo verticale riscritto row-major (una scanline alla volta); nessun accesso
  column-major. Harness dedicato con gli stessi loop e `-ffp-contract=off`:
  **bitwise identical**, 638 ms → 295 ms, **2.17x** (1920x1080, radius 24).
- `chronon3d_render_graph_tests` **469/469 PASS** (inclusi glass backdrop e
  i nuovi test LUT); nessun FAIL residuo.

### C3D-AUDIT-06 — evidenza

- Tutti i `std::getenv` per-frame/per-nodo diagnostic sono ora `static const`
  (node, executor, Vulkan operations, kernel replay, CUDA bridge, blur header).
- I `std::getenv` residui in `src/` sono startup/config (scheduler, CPU budget,
  ISA forcing, single-flight) o build-time scene/compiler path.
- Rimozione dei fallback GPU↔CPU per-frame: **NON FATTA**.
