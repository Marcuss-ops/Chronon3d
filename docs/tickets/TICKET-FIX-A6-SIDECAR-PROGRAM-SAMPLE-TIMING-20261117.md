# TICKET-FIX-A6-SIDECAR-PROGRAM-SAMPLE-TIMING-20261117 — A6: Esposizione di `program_sample_wall_us` e correzione di `timeline_eval_ms = 0.0` nel sidecar JSON sul percorso `CompiledProgram`

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P1 (Accuratezza strumentale del timing sidecar di produzione)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `apps/chronon3d_cli/commands/video/exporters/video_export_native_support.cpp` (riga 496)
  - `src/runtime/render_pipeline.cpp` (riga 122)
- **Owner:** C3D CLI / Telemetry maintainer

---

## 1. Diagnosi Esatta del Problema

In `apps/chronon3d_cli/commands/video/exporters/video_export_native_support.cpp:496`:

```cpp
timings.cpu_breakdown.timeline_eval_ms =
    static_cast<double>(c->timeline_eval_wall_us.load(std::memory_order_relaxed)) / 1000.0;
```

**Difetto verificato**:
Dopo la demolizione del legacy `Composition` executor (2026-11-10), l'unico percorso di esecuzione in produzione è `CompiledProgram` (`render_program` in `src/runtime/render_pipeline.cpp`).
Sul percorso `CompiledProgram`, il tempo speso per campionare i parametri del frame (`TypedProgramSampler::sample_into()`) e materializzare la scena viene accumulato nel contatore atomico `c->program_sample_wall_us` (`src/runtime/render_pipeline.cpp:122`), mentre `c->timeline_eval_wall_us` non viene mai incrementato e resta a `0`.
Di conseguenza, il sidecar `*.timing.json` emette `timeline_eval_ms: 0.0`, nascondendo completamente il costo CPU di campionamento del programma compilato.

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo 1 — Propagare `program_sample_wall_us` nel breakdown CPU del sidecar
In `apps/chronon3d_cli/commands/video/exporters/video_export_native_support.cpp:496`:
1. Leggere sia `timeline_eval_wall_us` sia `program_sample_wall_us`:
   ```cpp
   const auto timeline_us = c->timeline_eval_wall_us.load(std::memory_order_relaxed);
   const auto program_sample_us = c->program_sample_wall_us.load(std::memory_order_relaxed);
   timings.cpu_breakdown.timeline_eval_ms =
       static_cast<double>(timeline_us != 0 ? timeline_us : program_sample_us) / 1000.0;
   ```
2. Se lo schema `timing_sidecar_v2` prevede o accetta il campo esplicito `program_sample_ms`, valorizzarlo direttamente a fianco di `timeline_eval_ms` (verificando con `chronon3d_cli_tests` la conformità allo schema canonico `timing_sidecar_v2_is_the_only_schema`).

---

## 3. Demolition Debt

Nessuno.

---

## 4. Verifica e Criteri di Chiusura

1. **Test Sidecar CLI**:
   - `chronon3d_cli_tests` (251/251) verde.
   - Eseguire un render tramite `chronon3d_cli` con `--timing-sidecar` e verificare che `cpu_breakdown.timeline_eval_ms > 0.0` rifletta i microsecondi reali di `program_sample_wall_us`.
