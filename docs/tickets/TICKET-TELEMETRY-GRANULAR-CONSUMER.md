# TICKET-TELEMETRY-GRANULAR-CONSUMER — granular emitter non gated e store orfani

**Area:** runtime telemetry / render-graph executor
**Pri:** P2
**Stato:** OPEN (finding verificato 2026-09-05, su `main` HEAD)
**Owner:** telemetry (single-authority)

## Finding (verificato sul codice, non doc-claim)

1. **L'emissione granulare non è gated dal livello.** In build con
   `CHRONON3D_ENABLE_TELEMETRY=ON` (preset `linux-ci`, `linux-dev`),
   `emit_node_records()` viene eseguita per ogni nodo × ogni frame dai due
   call-site in `node_runner_single_node_detail.hpp` (cache-hit fast path +
   tail), **anche quando il run è `Summary`** — dove il contratto prevede solo
   `render_runs`/counters/summary. Costo per chiamata: 3× `node.name()` /
   `to_string(kind)` / `layer_id()` + push con lock su shard
   (`ShardedTelemetryStore::record`, header `render_telemetry.hpp`).
   Il gating `TelemetryLevel >= Detailed` esiste solo nel write-path SQLite di
   `record_run`; il capture-path in-memory è incondizionato.

2. **Gli store granulari in-memory non hanno consumer.** `collect_node_telemetry()`,
   `collect_cache_telemetry()`, `collect_layer_telemetry()` (e culling/image)
   hanno **zero call-site** in `src/`, `apps/`, `include/` (grep completo).
   I record sharded vengono scritti ma mai drenati né in SQLite né altrove →
   a livello `Summary` sono costo puro scartato; a `Detailed/Trace` non
   raggiungono comunque la persistenza (le tabelle granulari SQLite vengono
   popolate da un altro percorso). In render lunghi in build telemetry-ON lo
   store cresce in memoria senza bound (nessun `clear()`).

3. Nota di contesto: `emit_node_records` è compilata SOLO sotto
   `CHRONON3D_ENABLE_TELEMETRY` (source-list in `src/render_graph/CMakeLists.txt`),
   quindi nei build production telemetry-OFF il costo è zero. Il problema è
   limitato ai build telemetry-ON (CI debug, dev).

## Root cause architetturale

Il flusso singola-authority chiuso in TELEMETRY-SQLITE-NORMALIZATION è
`snapshot → record_run → SQLite`. Il vecchio percorso di capture granulare
(`record_*` header store → (nessuno) ) è sopravvissuto come residuo:
nessun migratore collega l'emitter alla persistenza, e il gate di livello non
è stato portato dal write-path al capture-path.

## Opzioni di fix (da decidere, non applicate)

- **A — gate sul capture-path**: armare un flag `detail_capture` a run-start
  (level >= Detailed) e fare early-out in `emit_node_records` quando spento.
  Richiede stato al confine runtime↔graph (il manager è in runtime, l'emitter
  in graph_executor) — niente dipendenza circolare, header-only state + setter
  chiamato da `TelemetryManager`.
- **B — drain reale a Detailed**: a `record_run` (level >= Detailed) collettare
  gli store e scriverli nelle tabelle eventi SQLite (render_phase_events /
  node event). Recupera il dato granulare che oggi è scartato.
- **C — demolizione**: se le tabelle granulari per-nodo/per-frame non sono un
  requisito di prodotto, rimuovere emitter + store sharded orfani (exit
  condition: zero consumer e zero requisiti).

La combinazione **A + B** è quella coerente col closeout telemetry:
Summary = zero costo granulare, Detailed = dato persistito.

## Exit condition

- Summary: `emit_node_records` non esegue lock/alloc/string per nodo.
- Detailed: i record granulari raggiungono SQLite (o vengono rimossi con C).
- Zero store sharded orfani (ogni `record_*` ha un `collect_*`/drain).
