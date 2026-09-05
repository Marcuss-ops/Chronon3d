# TICKET-TELEMETRY-SQLITE-NORMALIZATION — Architettura Unificata Telemetria & SQLite Store

**Stato:** OPEN (In Progress)  
**Priorità:** P1  
**Data:** 2026-09-05  
**Owner:** Core Runtime / Telemetry Working Session  

---

## 1. Principi Fondamentali e Authority

Chronon non ha bisogno di un nuovo framework metrics né di un secondo database layer. Chronon possiede già quasi tutti i mattoni necessari (`RenderCounters`, `NodeMemoryTracker`, `ShardedTelemetryStore`, `TelemetryManager`, `TelemetryStore`, `SQLiteTelemetryStore`).

La regola fondante dell'architettura è:

> **SQLite non misura niente. SQLite conserva soltanto il risultato delle authority che hanno già misurato.**

Non è consentita alcuna sovrapposizione di authority:

| Dominio / Dato | Authority Unica | Ruolo |
|---|---|---|
| Contatori globali hot-path | `RenderCounters` | Thread-local (`RenderCountersRaw TLS`) aggregati via `merge_tls()` |
| Memoria nodo & pool | `NodeMemoryTracker` | Allocazioni, byte read/written, buffer temporanei, peak RSS, stats pool |
| Tempo ed esecuzione nodo | `NodeTelemetryRecord` | Durata esecuzione, hit/miss cache, campionamento per-nodo |
| Comportamento cache | `CacheTelemetryRecord` | Metriche per tipo di cache (lookup, hit, miss, eviction) |
| Eventi layer | `LayerTelemetryRecord` | Bounds, trasformazioni, culling |
| Culling | `CullingTelemetryRecord` | Scelte di inclusione/esclusione frustum e visibility |
| Immagini / decode | `ImageTelemetryRecord` | Decode cost, sampled pixels, dimensioni |
| Persistenza & storage standard | `TelemetryStore` / `SQLiteTelemetryStore` | Scrittura batch atomica su disco a fine run |
| Orchestrazione | `TelemetryManager` | Boundary del lifecycle e coordinamento store |

### Regola anti-duplicazione
Nessun dato viene misurato due volte:
- `NodeMemoryTracker` è l'unica authority per bytes/allocations. `NodeTelemetryRecord` non re-inventa una memoria parallela.
- Non si scrive direttamente su SQLite saltando il `TelemetryManager`.
- La direzione è strettamente unidirezionale:
  measurement (RAM/TLS) -> snapshot (Value Object) -> TelemetryManager -> TelemetryStore -> SQLite

---

## 2. Invariante Hot-Path: Zero SQLite durante il Render

Durante `execute_node()` o nei cicli di rendering di ciascun frame:
- **Consentito**: Incrementi TLS (`++`), operazioni atomiche rilassate (`std::memory_order_relaxed`), bookkeeping su `NodeMemoryTracker`, push di eventi in RAM (`vector::emplace_back`).
- **Tassativamente vietato**: `sqlite3_step`, `sqlite3_prepare`, query SQL, apertura di file, `fsync`, lock di database o allocazioni su heap non controllate.

Il rendering procede a piena velocità senza alcuna consapevolezza dell'esistenza del database.

---

## 3. End-of-Render Flow e Barrier Atomica

A rendering completato, prima della cattura dei dati:

```text
render_video() completato
     │
     ▼
All workers joined / synchronized (Barriera)
     │
     ▼
Flush worker TLS ───► merge_tls() ───► Snapshot RenderCounters
     │
     ├────────────────────────────────────────┐
     │                                        │
     ▼                                        ▼
NodeMemoryTracker::snapshot()       collect_*_telemetry()
(peak memory, pool stats,           (drenaggio ShardedTelemetryStores
 node memory stats)                  senza lock)
     │                                        │
     └───────────────────┬────────────────────┘
                         ▼
             TelemetryRunSnapshot
           (Value Object Immutabile)
                         │
                         ▼
        TelemetryManager::record_run(snapshot)
                         │
                         ▼
               SQLiteTelemetryStore
                         │
        BEGIN IMMEDIATE TRANSACTION
                         │
             1. Scrittura Run KPI & Identity
             2. Scrittura RenderCounters normalizzati
             3. Scrittura render_node_summary (join Node + Memory)
             4. Scrittura render_memory_summary
             5. Scrittura opzionale eventi dettagliati (se Detailed/Trace)
             6. Scrittura Artifacts
                         │
                  COMMIT / ROLLBACK
```

**One run = One atomic transaction**: il run viene persistito interamente oppure rollbackato; nessuna riga orfana o stato inconsistente.

---

## 4. Normalizzazione del Modello Dati e Tabelle

### 4.1. Riduzione di `render_runs`
`render_runs` cessa di essere una tabella da oltre 131 colonne che mescola identità e contatori effimeri. Diventa la tabella identificativa del run:
- `run_id`, `started_at`, `finished_at`
- `composition_id`, `workload_fingerprint`, `hardware_fingerprint`
- `success`, `error_code`
- `frames_total`, `frames_written`, `width`, `height`
- `wall_ms`, `render_ms`, `encode_ms`, `effective_fps`
- `peak_rss_bytes`, `peak_vram_bytes`
- `git_commit`, `build_type`, `backend`, `quality_mode`

### 4.2. `render_counters` per i contatori dinamici
Tutti i contatori specifici di nodi, shader, clear, fallback e pool finiscono nella tabella relazionale:
```sql
CREATE TABLE IF NOT EXISTS render_counters (
    run_id TEXT NOT NULL,
    counter_name TEXT NOT NULL,
    counter_value INTEGER NOT NULL,
    PRIMARY KEY (run_id, counter_name)
);
```

### 4.3. Proiezioni Persistenti a Fine Run
1. **`render_node_summary`**: join a fine run tra `NodeTelemetry` (calls, duration, hits) e `NodeMemoryTracker` (bytes read/written, allocations, peak_live_bytes). Diventa la tabella primaria per individuare regressioni algoritmiche o mem-leak per tipo di nodo.
2. **`render_memory_summary`**: snapshot consolidato di `NodeMemoryReport` (peak RSS, live memory, allocazioni e reuse del FramebufferPool).

---

## 5. Livelli di Telemetria (TelemetryLevel)

1. **`Off`**: Persistenza disabilitata.
2. **`Summary` (Default di produzione)**:
   - `render_runs`
   - `render_counters`
   - `render_node_summary`
   - `render_memory_summary`
   - `render_phase_events`
   - `render_artifacts`
   *(< 1.000 righe per run, scalabile su decine di migliaia di render)*
3. **`Detailed` (CI, Performance testing, Benchmarks)**:
   - Aggiunge `render_frames`, `render_node_events`, `render_layer_events`, `render_cache_events`, `render_culling_events`, `render_image_events`, `render_memory_samples`.
4. **`Trace`**:
   - Abilita tracciamenti esterni (es. Perfetto, Chrome Trace) registrati in SQLite esclusivamente come puntatori di artefatti (`trace_path`, `trace_hash`, `trace_size`).

---

## 6. Piano di Implementazione a 7 Stage

- [x] **Stage 1 — Schema Truth**: Eliminazione della doppia definizione dello schema in C++ (`RUN_COLUMN_NAMES`, `FRAME_COL_NAMES`, ecc.) e rimozione di `migrate_add_missing_columns` a favore dello schema SQL canonico con `user_version` PRAGMA.
- [x] **Stage 2 — TelemetryRunSnapshot**: Creazione del Value Object immutabile `TelemetryRunSnapshot` in `include/chronon3d/runtime/telemetry/telemetry_run_snapshot.hpp` e overload in `TelemetryManager::record_run(const TelemetryRunSnapshot&)`.
- [x] **Stage 3 — Memory Persistence**: Aggiunta delle tabelle SQL `render_node_summary` e `render_memory_summary`, con binding preparati in `sqlite_telemetry_store_events.cpp` e supporto in `TelemetryStore`.
- [ ] **Stage 4 — Run Schema Normalization**: Deprecazione delle 131 colonne di `render_runs` a favore di `render_counters` e summary mirati.
- [ ] **Stage 5 — Telemetry Levels**: Implementazione dell'enum `TelemetryLevel` e filtraggio del salvataggio degli eventi granulari in `Summary` mode.
- [x] **Stage 6 — Profiler Consolidation & Demolition Debt**: Consumer census di `RenderProfiler` eseguito (0 chiamanti attivi); `RenderProfiler` (`graph_profiler.hpp`, `graph_profiler.cpp` e membro in `RenderGraphContext`) rimosso definitivamente dal core.
- [ ] **Stage 7 — Hardware & Workload Fingerprinting**: Riconoscimento hardware dettagliato (CPU/GPU/Driver) e calcolo degli hash canonici per cost model e regression testing.

---

## 7. Criteri di Accettazione e Demolition Debt

1. **Nessuna doppia authority** per memoria o contatori.
2. **Nessun accesso a SQLite** lungo l'hot path di rendering.
3. **One run = One atomic transaction** verificato con test di persistenza e failover.

### Scheda di Demolition Debt 1: `RenderProfiler` (Legacy Profiler Subsystem)
- **Owner**: Core Runtime / Telemetry Working Session
- **Reason**: Storicamente usato come trace-buffer locale per `NodeProfile` e per generare report JSON ad-hoc.
- **Exit condition**: Consumer census completato (0 chiamanti attivi di `record_node`, `record_node_tls`, `history`, `generate_report_json`); `NodeMemoryTracker` e `NodeTelemetryRecord` coprono il 100% dell'osservabilità necessaria.
- **Equivalence test/gate**: `chronon3d_renderer_core_tests` (la suite di telemetria e snapshot copre l'interezza delle metriche senza passare dal profiler).
- **Removal scope**:
  - `include/chronon3d/render_graph/core/graph_profiler.hpp`
  - `src/render_graph/graph_profiler.cpp`
  - Membri in `RenderGraphContext` (`RenderProfiler* profiler`)
  - Target in `src/render_graph/CMakeLists.txt`
- **Status**: `REMOVED`

### Scheda di Demolition Debt 2: Legacy Dynamic Column Top-up (`migrate_add_missing_columns`)
- **Owner**: Core Runtime / Telemetry Working Session
- **Reason**: Compatibilità transitoria per database SQLite creati prima del PRAGMA `user_version`.
- **Exit condition**: Tutti i DB in uso migrati ad almeno `user_version >= 1`.
- **Equivalence test/gate**: Test di bootstrap DB pulito e test di apertura DB versionato in `tests/runtime/test_telemetry.cpp`.
- **Removal scope**:
  - Array di colonne C++ (`RUN_COLUMN_NAMES`, `FRAME_COL_NAMES`, `ALL_TABLES`, ecc.)
  - Funzione `migrate_add_missing_columns()` in `src/runtime/telemetry/sqlite/sqlite_telemetry_store_schema.cpp`.
- **Status**: `ACTIVE`
