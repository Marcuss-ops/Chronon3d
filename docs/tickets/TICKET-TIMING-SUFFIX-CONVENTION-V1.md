# TICKET-TIMING-SUFFIX-CONVENTION-V1 — Catalogo + rename map per i suffissi `*_wall_ms`/`*_cpu_ms`/`*_gpu_ms`/`*_wait_ms`

## Stato: IN CORSO (catalogo su `main`; GRAPH, NODES/COMPOSITE+EFFECTS+FRAMEBUFFER+DIRTY+TILE, TEXT e VIDEO rinominati; restanti domini forward-pointed)

## Problema

La regola di sistema imposta dal piano di telemetria è:

> **Nessun timing aggregato deve essere chiamato semplicemente `render_ms` o
> `encoder_ms` se non è chiaro se rappresenta wall time, CPU accumulated time
> o GPU elapsed time.** Usa suffissi espliciti: `*_wall_ms`, `*_cpu_ms`,
> `*_gpu_ms`, `*_wait_ms`.

Oggi la maggior parte dei counter di timing della X-macro `CHRONON_RENDER_COUNTERS`
usa un `*_ms` / `*_us` **senza** qualificatore semantico (`graph_execute_ms`,
`clearnode_ms`, `video_conversion_ms`, …). Quando avremo CPU, Vulkan, encoder
asincrono e 100 worker contemporaneamente, non sarà più possibile distinguere
wall/cpu/gpu/wait. Questo ticket è il **catalogo canonico** che:

1. definisce la convenzione con precisione (inclusa la gestione del `_us`);
2. classifica ogni counter di timing per **semantica de-facto**;
3. fornisce la **rename map completa** old → new, pronta per l'esecuzione
   dominio-per-dominio (i forward-point sotto).

## Convenzione (definizione esatta)

Un counter è **"di timing"** se misura una **durata** (vs un conteggio di eventi
o un gauge di stato/byte). Ogni counter di timing DEVE terminare con **uno** dei
quattro qualificatori semantici:

| Suffisso | Significato |
| -------- | ----------- |
| `*_wall_ms` / `*_wall_us` | wall-clock elapsed (steady_clock) |
| `*_cpu_ms`  / `*_cpu_us`  | CPU time (process/thread, o costo CPU di una submit) |
| `*_gpu_ms`  / `*_gpu_us`  | GPU elapsed (Vulkan timestamp query) |
| `*_wait_ms` / `*_wait_us` | attesa bloccante (coda, poll, fence, mutex) |

Nuance di unità: il qualificatore semantico è **obbligatorio**, l'unità
(`_ms` vs `_us`) resta libera. I counter `dof_*_us` misurano durate sub-ms e
NON vanno convertiti a `_ms` (perderebbero precisione): vanno portati a
`dof_*_wall_us`, mantenendo l'unità microsecondo.

I suffissi secondari (`_sum`, `_count`, `_per_frame`, `_pct`, `_mb`, `_bytes`,
`_peak`) possono seguire il qualificatore semantico: es. `graph_executed_ms_sum`
→ `graph_executed_wall_ms_sum`.

## Metodologia di classificazione (evidenza)

Semantica de-facto dedotta dalla sorgente del timer:

- **WALL** — `profiling::now()` / `elapsed_ms()` / `elapsed_us()` /
  `duration_ms()` / `timestamp_ns()` (tutti alias di `std::chrono::steady_clock`)
  oppure `std::chrono::steady_clock::now()` diretto. Esempi verificati:
  `clear_node.cpp` (`clearnode_ms`), `level_timings.cpp`
  (`node_execute_actual_ms` + 11 contatori di level-rollup),
  `scroll_optimization.cpp` (`scroll_opt_copy_ms`),
  `video_sink_adapter.cpp` (`video_conversion_ms`), `packed_backend.cpp` /
  `swscale_backend.cpp` (`pixel_format_convert_ms` / `color_space_convert_ms`).
- **CPU** — `process_cpu_user_ms` / `process_cpu_sys_ms` (sampler `/proc`);
  `native_av_send_frame_ms` (= puro `avcodec_send_frame`, costo CPU di submit).
- **GPU** — solo i counter exportati dal backend Vulkan via
  `export_gpu_telemetry_counters` (`gpu_execute_us` da `VkQueryPool`); NESSUN
  counter X-macro è GPU-elapsed oggi.
- **WAIT** — attesa su coda/poll/fence: `io_queue_push_blocked_ms`,
  `io_queue_pop_wait_ms`, `io_writer_idle_wait_ms`, `video_writer_wait_ms`,
  `video_ffmpeg_latency_ms` (doc: "blocked waiting for FFmpeg to drain"),
  `encoder_backpressure_wait_ms`.

Fatto saliente: **~80 dei contatori ambigui sono WALL** (steady_clock). I casi
CPU/WAIT sono pochi e puntuali, elencati di seguito.

## Inventario completo + rename map

### Testo (`CHRONON_COUNTERS_TEXT`)

| Attuale | Semantica | Nuovo |
| ------- | --------- | ----- |
| `text_layout_ms` | wall | `text_layout_wall_ms` |
| `text_rasterization_ms` | wall | `text_rasterization_wall_ms` |
| `text_shaping_ms` | wall | `text_shaping_wall_ms` |
| `text_bidi_ms` | wall | `text_bidi_wall_ms` |

### Tile (`CHRONON_COUNTERS_TILE`)

| Attuale | Semantica | Nuovo |
| ------- | --------- | ----- |
| `tile_execution_ms` | wall | `tile_execution_wall_ms` |

### Composite / Clear (`CHRONON_COUNTERS_COMPOSITE`)

| Attuale | Semantica | Nuovo |
| ------- | --------- | ----- |
| `clearnode_ms` | wall | `clearnode_wall_ms` |
| `clearnode_memcpy_ms` | wall | `clearnode_memcpy_wall_ms` |
| `clearnode_restore_ms` | wall | `clearnode_restore_wall_ms` |
| `clearnode_acquire_ms` | wall | `clearnode_acquire_wall_ms` |
| `clearnode_clear_ms` | wall | `clearnode_clear_wall_ms` |
| `compositenode_blend_ms` | wall | `compositenode_blend_wall_ms` |
| `compositenode_setup_ms` | wall | `compositenode_setup_wall_ms` |
| `compositenode_copy_ms` | wall | `compositenode_copy_wall_ms` |
| `compositenode_row_ms` | wall | `compositenode_row_wall_ms` |
| `compositenode_dispatch_ms` | wall | `compositenode_dispatch_wall_ms` |
| `compositenode_acquire_ms` | wall | `compositenode_acquire_wall_ms` |
| `compositenode_overhead_ms` | wall | `compositenode_overhead_wall_ms` |
| `compositenode_internal_us` | wall (us) | `compositenode_internal_wall_us` |

### Effects (`CHRONON_COUNTERS_EFFECTS`)

| Attuale | Semantica | Nuovo |
| ------- | --------- | ----- |
| `effect_stack_total_ms` | wall | `effect_stack_total_wall_ms` |
| `dof_roi_analysis_us` | wall (us) | `dof_roi_analysis_wall_us` |
| `dof_blur_radius_generation_us` | wall (us) | `dof_blur_radius_generation_wall_us` |
| `dof_scratch_allocation_us` | wall (us) | `dof_scratch_allocation_wall_us` |
| `dof_copy_to_hpass_us` | wall (us) | `dof_copy_to_hpass_wall_us` |
| `dof_horizontal_pass_us` | wall (us) | `dof_horizontal_pass_wall_us` |
| `dof_hpass_to_output_us` | wall (us) | `dof_hpass_to_output_wall_us` |
| `dof_vertical_pass_us` | wall (us) | `dof_vertical_pass_wall_us` |
| `dof_writeback_us` | wall (us) | `dof_writeback_wall_us` |
| `effect_focus_in_ladder_precompute_ms` | wall | `effect_focus_in_ladder_precompute_wall_ms` |
| `effect_focus_in_ladder_crossfade_ms` | wall | `effect_focus_in_ladder_crossfade_wall_ms` |

### Framebuffer (`CHRONON_COUNTERS_FRAMEBUFFER`)

| Attuale | Semantica | Nuovo |
| ------- | --------- | ----- |
| `framebuffer_pool_clear_ms` | wall | `framebuffer_pool_clear_wall_ms` |
| `framebuffer_enqueue_ms` | wall | `framebuffer_enqueue_wall_ms` |
| `framebuffer_copy_ms` | wall | `framebuffer_copy_wall_ms` |

### Dirty (`CHRONON_COUNTERS_DIRTY`)

| Attuale | Semantica | Nuovo |
| ------- | --------- | ----- |
| `scroll_opt_copy_ms` | wall | `scroll_opt_copy_wall_ms` |

### Graph (`CHRONON_COUNTERS_GRAPH`)

| Attuale | Semantica | Nuovo |
| ------- | --------- | ----- |
| `graph_resolve_layers_ms` | wall | `graph_resolve_layers_wall_ms` |
| `graph_dirty_rect_ms` | wall | `graph_dirty_rect_wall_ms` |
| `graph_build_ms` | wall | `graph_build_wall_ms` |
| `graph_execute_ms` | wall | `graph_execute_wall_ms` |
| `graph_total_ms` | wall | `graph_total_wall_ms` |
| `timeline_eval_ms` | wall | `timeline_eval_wall_ms` |
| `compiled_graph_refresh_ms` | wall | `compiled_graph_refresh_wall_ms` |
| `cache_eval_ms` | wall | `cache_eval_wall_ms` |
| `dirty_eval_ms` | wall | `dirty_eval_wall_ms` |
| `input_resolve_ms` | wall | `input_resolve_wall_ms` |
| `framebuffer_lifetime_ms` | wall | `framebuffer_lifetime_wall_ms` |
| `node_schedule_ms` | wall | `node_schedule_wall_ms` |
| `node_dispatch_ms` | wall | `node_dispatch_wall_ms` |
| `node_execute_actual_ms` | wall | `node_execute_actual_wall_ms` |
| `node_overhead_ms` | wall | `node_overhead_wall_ms` |
| `telemetry_emit_ms` | wall | `telemetry_emit_wall_ms` |
| `predicted_bbox_ms` | wall | `predicted_bbox_wall_ms` |
| `clone_context_ms` | wall | `clone_context_wall_ms` |
| `state_assign_ms` | wall | `state_assign_wall_ms` |
| `framebuffer_acquire_ms` | wall | `framebuffer_acquire_wall_ms` |
| `framebuffer_clear_ms` | wall | `framebuffer_clear_wall_ms` |
| `graph_executed_ms_sum` | wall | `graph_executed_wall_ms_sum` |
| `graph_skipped_ms_sum` | wall | `graph_skipped_wall_ms_sum` |

### Video (`CHRONON_COUNTERS_VIDEO`)

| Attuale | Semantica | Nuovo |
| ------- | --------- | ----- |
| `frame_conversion_copy_ms` | wall | `frame_conversion_copy_wall_ms` |
| `video_graph_eval_ms` | wall | `video_graph_eval_wall_ms` |
| `video_conversion_ms` | wall | `video_conversion_wall_ms` |
| `video_pipe_write_ms` | wall | `video_pipe_write_wall_ms` |
| `video_ffmpeg_latency_ms` | **wait** | `video_ffmpeg_wait_ms` |
| `io_queue_push_blocked_ms` | **wait** | `io_queue_push_wait_ms` |
| `ffmpeg_pipe_write_blocked_ms` | wall | `ffmpeg_pipe_write_wall_ms` |
| `ffmpeg_flush_ms` | wall | `ffmpeg_flush_wall_ms` |
| `native_av_convert_ms` | wall | `native_av_convert_wall_ms` |
| `native_av_send_frame_ms` | **cpu** | `native_av_send_frame_cpu_ms` |
| `native_av_receive_packet_ms` | wall | `native_av_receive_packet_wall_ms` |
| `native_av_mux_write_ms` | wall | `native_av_mux_write_wall_ms` |
| `native_av_trailer_ms` | wall | `native_av_trailer_wall_ms` |
| `native_av_convert_skipped_ms` | wall | `native_av_convert_skipped_wall_ms` |
| `video_convert_only_ms` | wall | `video_convert_only_wall_ms` |
| `video_pipe_write_only_ms` | wall | `video_pipe_write_only_wall_ms` |
| `frame_conversion_ms` | wall | `frame_conversion_wall_ms` |
| `frame_submit_ms` | wall | `frame_submit_wall_ms` |
| `encoder_flush_ms` | wall | `encoder_flush_wall_ms` |
| `mux_finalize_ms` | wall | `mux_finalize_wall_ms` |
| `pixel_format_convert_ms` | wall | `pixel_format_convert_wall_ms` |
| `color_space_convert_ms` | wall | `color_space_convert_wall_ms` |

Già conformi (esclusi dal rename): `io_queue_pop_wait_ms`,
`io_writer_idle_wait_ms`, `video_writer_wait_ms` (wait), `encoder_submit_cpu_ms`,
`pipe_write_cpu_ms` (cpu), `encoder_backpressure_wait_ms` (wait),
`pipe_write_wall_ms` (wall).

### System (`CHRONON_RENDER_COUNTERS_SYSTEM`)

| Attuale | Semantica | Nuovo |
| ------- | --------- | ----- |
| `sequential_level_execute_ms` | wall | `sequential_level_execute_wall_ms` |

Già conformi: `process_cpu_user_ms` / `process_cpu_sys_ms` (CPU, chiaro),
`ffmpeg_cpu_user_pct` / `ffmpeg_cpu_sys_pct` (CPU %, gauge).

### Setup (`CHRONON_RENDER_COUNTERS_SETUP`)

| Attuale | Semantica | Nuovo |
| ------- | --------- | ----- |
| `setup_graph_parsing_ms` | wall | `setup_graph_parsing_wall_ms` |
| `setup_asset_io_load_ms` | wall | `setup_asset_io_load_wall_ms` |
| `setup_pool_preallocation_ms` | wall | `setup_pool_preallocation_wall_ms` |
| `image_decode_ms` | wall | `image_decode_wall_ms` |

Totale: **83** counter di timing da rinominare (incluso `timeline_eval_ms`, inizialmente omesso e recuperato in TICKET-TIMING-SUFFIX-GRAPH).

## Esclusi (non-timing — la regola NON si applica)

Contatori che non misurano una durata: tutti i `*_count`/`*_calls`/`*_pixels`/
`*_hits`/`*_misses`/`*_bytes`/`*_allocations`/`*_reuses`/`*_peak`/`*_depth`, e i
gauge come `dof_max_radius_milli` (raggio, non tempo), `program_cache_capacity`,
`program_cache_tune`, `video_sink_type_id`, `encoder_slots_allocated`.

## Superficie secondaria (fuori dalla X-macro, da allineare separatamente)

1. **Backend GPU export** (`VulkanBackend::export_gpu_telemetry_counters`):
   `gpu_submit_cpu_us` (cpu ✓), `gpu_wait_cpu_us` (wait, proposto
   `gpu_wait_wait_us`… vedi decisione sotto), `readback_us` → `readback_wall_us`,
   `cpu_gpu_sync_us` → `cpu_gpu_sync_wait_us`, `gpu_execute_us` → GPU-elapsed
   (`gpu_execute_gpu_us`). Richiede una decisione sui nomi GPU perché
   `_gpu_us` + `gpu_` prefix è ridondante.
2. **Bench JSON `chronon3d.bench.v3`** (`BenchmarkMetrics` +
   `BenchmarkCountersSnapshot`): `time_to_first_frame_ms`, `avg_frame_ms`,
   `median_frame_ms`, `min/max_frame_ms`, `p50/p95/p99_frame_ms`,
   `conversion_ms`, `fps`/`fps_steady_state` — tutte durate wall implicite. Fuori
   dallo scope del rename X-macro, ma da documentare/valutare.

## Note di consolidamento (overlap con TICKET-VIDEO-PIPELINE-BACKPRESSURE-V1)

Il rename espone ridondanze introdotte dal breakdown video:

- `native_av_send_frame_ms` ≡ `encoder_submit_cpu_ms` (stesso valore).
- `native_av_trailer_ms` ≡ `mux_finalize_ms`.
- `native_av_convert_ms` ≡ `pixel_format_convert_ms + color_space_convert_ms`.

L'esecuzione VIDEO può quindi **rinominare e ritirare** i legacy `native_av_*`
a favore dei nuovi espliciti, invece di tenere due coppie vive.

Eseguito (TICKET-TIMING-SUFFIX-VIDEO): `native_av_send_frame_ms` e
`native_av_trailer_ms` **ritirati** (alias esatti di `encoder_submit_cpu_ms` /
`mux_finalize_wall_ms`); `native_av_convert_ms` **tenuto** come
`native_av_convert_wall_ms` perché è un punto di misura distinto dal servizio di
conversione frame (la somma `pixel_format_convert_wall_ms +
color_space_convert_wall_ms` è solo ≈, non ≡). I phase-label per-job
(`phases[]` in `pipe_export_finalize.cpp`) e i field delle struct di timings
(`FrameEncoderTelemetry`, `JobTimings`, breakdown per-frame) restano fuori scope
come superficie secondaria.

## Criteri di accettazione (per l'esecuzione complessiva)

- [ ] Ogni counter di timing X-macro termina con uno dei 4 qualificatori.
- [x] Il gate (forward-point) fallisce su qualsiasi nuovo `*_ms`/`*_us` di timing
      privo di qualificatore wall/cpu/gpu/wait.
- [ ] Le unità `_us` restano `_us` (solo il qualificatore semantico è aggiunto).
- [ ] Nessun cambio di semantica: i nuovi nomi sono alias esatti dei vecchi
      valori (rename meccanico, nessun re-wiring di logica).
- [ ] Dashboard, SQLite, tooling perf e test aggiornati coerentemente.
- [ ] WBH build + `ctest` + render reale senza riferimenti rotti.

## Forward-points (esecuzione, dominio per dominio, tutte su `main`)

- **TICKET-TIMING-SUFFIX-GRAPH** — rename 22 counter del dominio GRAPH (+ level
  roll-up) e tutti i siti di wiring/surfacing. ✅ DONE (su `main`).
- **TICKET-TIMING-SUFFIX-NODES** — rename COMPOSITE(13) + EFFECTS(11) +
  FRAMEBUFFER(3) + DIRTY(1) + TILE(1). ✅ DONE (su `main`).
- **TICKET-TIMING-SUFFIX-TEXT** — rename 4 counter del dominio TEXT. ✅ DONE (su `main`).
- **TICKET-TIMING-SUFFIX-VIDEO** — rename 22 counter VIDEO + consolidamento
  `native_av_*` vs `encoder_*`/`pipe_*`. ✅ DONE (su `main`; 2 ritirati, 20 rinominati).
- **TICKET-TIMING-SUFFIX-SYSTEM-SETUP** — rename SYSTEM(1) + SETUP(4). ✅ DONE (su `main`;
  `sequential_level_execute_wall_ms` + `setup_graph_parsing_wall_ms`/`setup_asset_io_load_wall_ms`/
  `setup_pool_preallocation_wall_ms`/`image_decode_wall_ms`; pending list rimosso dal gate).
- **TICKET-TIMING-SUFFIX-SQLITE** — migrazione schema + store + `render_telemetry_record`. ✅ DONE (su `main`; colonne di `render_runs` + `render_telemetry_record` aggiornate inline per GRAPH/NODES/TEXT/VIDEO).
- **TICKET-TIMING-SUFFIX-DASHBOARD** — superseded: dashboard web rimossa (web-free).
- **TICKET-TIMING-SUFFIX-TOOLING** — `compare_telemetry.py`/`pr_gate.py`/
  `measure_cpu_budget.sh`/`render_job_report.cpp`. ✅ DONE (su `main`; aggiornati inline).
- **TICKET-TIMING-SUFFIX-GATE** — gate/test di enforcement (fail su `*_ms`/`*_us`
  di timing senza qualificatore) + whitelist dei counter di timing conformi. ✅ DONE (su `main`; test in `tests/core/test_render_counters.cpp` su `kCounterNames`).
- **TICKET-TIMING-SUFFIX-WBH-VERIFY** — build + `ctest` + render su host di build.
