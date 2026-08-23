# TICKET-TELEMETRY-STORE-CONSUMER-AUDIT — ShardedTelemetryStore consumer audit + elimination evaluation (Fase 7)

## Stato: DONE (2026-08-22; gating + physical removal landed)

## Problema (piano §20)

> "Non la cancellerei insieme a Tracy. Prima bisogna verificare tutti i suoi
> consumer: sidecar, hot-node report, tests, SQLite, debug diagnostics. Poi
> possiamo eventualmente arrivare a: `RenderCounters → metriche aggregate`,
> `Perfetto → eventi dettagliati` ed eliminare buona parte degli event store
> con mutex."

Obiettivo della Fase 7: verificare **tutti** i consumer degli event store
mutex-based (`detail::ShardedTelemetryStore<Record>`, 16 mutex × 7 categorie)
e valutare l'eliminazione lasciando Perfetto (timeline dettagliata) +
RenderCounters (aggregati).

## Inventory degli store

`include/chronon3d/core/telemetry/render_telemetry.hpp` — 7 store globali:

| Store | Record | Scrittore (writer) | Letto da |
|---|---|---|---|
| node | `NodeTelemetryRecord` | `telemetry_emitter.cpp` (per-nodo, `emit_node_records`) | bundle |
| layer | `LayerTelemetryRecord` | `telemetry_emitter.cpp` (per-nodo) | bundle |
| cache | `CacheTelemetryRecord` | `telemetry_emitter.cpp` (per-nodo) | bundle |
| culling | `CullingTelemetryRecord` | `graph_builder_layer_pipeline_pass.cpp` (per-layer, **unconditional**) | bundle |
| image | `ImageTelemetryRecord` | `image_renderer.cpp` (per-draw, **unconditional**) | bundle |
| text | `TextTelemetryRecord` | **nessuno** (zero writer in tutto il repo) | bundle |
| tile | `TileTelemetryRecord` | **nessuno** (zero writer in tutto il repo) | bundle |

Nota: `runtime/telemetry/telemetry_session.hpp` (`TelemetrySession`) è un
secondo collector per-render-job **senza alcun consumer attivo** (solo un
commento in `frame_timing_summary.hpp`) — codice morto/aspirazionale, non
alimenta gli store.

## Audit dei consumer (verificato su main@6210eca08)

### 1. Sidecar timing JSON — NON consumer
`pipe_timing_sidecar.cpp` usa solo counters + per-frame telemetry + phases.
Zero riferimenti agli event store (`grep node_events|layer_events|...` → 0).

### 2. Hot-node report — NON consumer
`render_job_finalize.cpp` usa `renderer->node_cache().top_entries_by_weight(10)`
(NodeCache, non gli store) + counters. Il bundle raccolto viene **scartato
subito**: `auto telemetry = collect_all_telemetry(); (void)telemetry;`.

### 3. Still/sequence finalize — consumer fantasma
Stesso file di (2): raccoglie tutti e 7 gli store (16 lock × 7 = 112
acquisizioni mutex) e butta il risultato. Nessun uso — nemmeno con SQLite ON
(`record_run(run, telemetry_frames, phases, counters_list)` non riceve eventi).

### 4. SQLite — UNICO consumer reale (gate `CHRONON3D_ENABLE_SQLITE_TELEMETRY`)
- `pipe_export_finalize.cpp`: `collect_all_telemetry()` → `record_output_run(...)`
  → `TelemetryManager::record_run` → `write_node/layer/cache/culling/text/image/tile_events`
  (tabelle SQLite, `sqlite_telemetry_store_events.cpp`). Il tutto dentro
  `#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY`.
- `video_export_chunked.cpp`: accumulo per-chunk degli eventi → stessa chiamata
  SQLite gated.
- `CHRONON3D_ENABLE_TELEMETRY` (che abilita il define nei target consumer)
  default **OFF**; nessun build/CI lo accende.

### 5. Tests — consumer del contratto dello store
- `tests/core/test_sharded_telemetry_store.cpp`: unit test della template
  (record/collect/clear/concorrenza).
- `tests/runtime/test_telemetry.cpp`: roundtrip record/collect node + MockStore
  con `TelemetryManager::record_run`.

### 6. Diagnostics — NON consumer
`test_graph_preflight_diagnostics.cpp` cita `CullingTelemetryRecord` solo in un
commento; la verifica usa il report proprio della preflight, non lo store.

### 7. Writers in build default (CHRONON3D_ENABLE_TELEMETRY=OFF)
- node/layer/cache: **non compilati** (`telemetry_emitter.cpp` è gated in
  `src/render_graph/CMakeLists.txt` da `if(CHRONON3D_ENABLE_TELEMETRY)`).
- culling (`graph_builder_layer_pipeline_pass.cpp`) e image
  (`image_renderer.cpp`): **scritti unconditionalmente** — mutex + push_back +
  string copy (image_path, layer name) per ogni layer/draw, poi il tutto viene
  drainato a fine job e scartato. Costo puro in ogni build default.

## Valutazione — eliminazione

**Verdetto: gli event store servono solo a SQLite (off by default) e ai test.
Nel default build il costo (mutex per-evento + drain 112-lock + bundle scartato)
è spreco puro.** Perfetto (livello `nodes`, Fase 1-3) copre già la timeline
per-nodo che node/layer/cache store fornivano; RenderCounters coprono gli
aggregati.

### Raccomandazione (implementata in questo ticket)
1. **Gate della registrazione**: culling + image writers dentro
   `#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY` (node/layer/cache sono già gated
   via TU) → in default build gli store restano **vuoti**.
2. **Gate della raccolta**: i 3 siti `collect_all_telemetry()` (still/seq,
   video pipe, chunked) dentro lo stesso define → niente drain cost in default.
3. **CMake**: `CHRONON3D_ENABLE_SQLITE_TELEMETRY` propagato a
   `chronon3d_graph_builder` + `chronon3d_backend_assets` sotto
   `if(CHRONON3D_ENABLE_TELEMETRY)` → il feature SQLite resta identico quando
   abilitato.
4. **Si tiene** lo store + `TelemetryManager` + tabelle SQLite + test: la
   feature esiste e ha schema/semantica propria.

### Rimozione fisica (landed 2026-08-22)
- **Rimossi i pair text/tile**: `TextTelemetryRecord` + `TileTelemetryRecord`
  (zero writer), store accessors/record/collect in `render_telemetry.hpp`,
  campi `text_events`/`tile_events` in `TelemetryBundle` e firma
  `record_output_run`/`record_run` (chirurgia API pubblica + schema SQLite
  `render_text_events`/`render_tile_events` + indici + `write_text_events`/
  `write_tile_events` su `TelemetryStore`/`NullTelemetryStore`/
  `SqliteTelemetryStore`).
- **Rimossa `TelemetrySession`** (`runtime/telemetry/telemetry_session.hpp`)
  + relativo test + voce CMake/baseline; commento in `frame_timing_summary.hpp`
  aggiornato a `FrameTelemetry`.
- Restano 5 store (node/layer/cache/culling/image) al servizio del solo
  consumer SQLite (default OFF) + RenderCounters/Perfetto.

### Deferred (follow-up)
- **Eliminazione totale** degli store quando Perfetto + RenderCounters
  copriranno il 100% dei consumer (dopo la certificazione del trace pipeline).
- Verifica WBH con `CHRONON3D_ENABLE_TELEMETRY=ON` (SQLite tables popolate)
  su un host con build telemetry.

## Verifiche eseguite
- Audit statico completo: scrittori/lettori elencati sopra (grep su
  `record_*_telemetry` / `collect_*_telemetry` / `collect_all_telemetry` /
  `clear_telemetry_stores` in src/include/apps/tests/tools).
- Build default (TELEMETRY=OFF) verde dopo il gating; TU gated syntax-checked
  con `-DCHRONON3D_ENABLE_SQLITE_TELEMETRY` (percorso ON compila).
- 26/26 architecture boundaries + doc-sync PASS.

## Forward-points
- `TICKET-TELEMETRY-STORE-REMOVAL`: rimozione fisica text/tile +
  TelemetrySession **landed** (2026-08-22, questo commit). Resta aperta la
  valutazione dell'eliminazione totale degli store quando Perfetto +
  RenderCounters copriranno il 100% dei consumer.
