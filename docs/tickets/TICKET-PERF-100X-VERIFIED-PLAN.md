# TICKET-PERF-100X-VERIFIED-PLAN — piano verificato per il salto di throughput overlay

**Stato (2026-11-03):** OPEN — `[CODICE]` P0 implementato; `[MISURA]` i job r103/r104/r105 e il job r101-like P4 sono riusciti sul worker `host_57_129_132_133` e hanno emesso telemetria P0. `[MISURA]` P1 **non supera il gate**: tutti e tre i run hanno `evaluation_mode=compiled_program`, `timeline_eval_ms=null` e SHA overlay diversi. `[MISURA]` P4 è parziale: il percorso compilato è attribuito e `program_sample_ms` è presente, ma non sono dimostrati né il gate di attribuzione wall ≥90% né l'invarianza SHA pre/post P0. `[NOT RUN]` test C++ dello schema e confronto SHA pre/post strumentazione. `[MISURA]` A14: due run r20minuti attribuiscono `prepare_join` al ramo semantico (`last_branch=semantic`, 86,9/60,0 s); il gate di ripetibilità ±10% **non passa** (−30,9%) e il contenuto è rigenerato.
**Area:** Chronon3d render graph/timeline e telemetria CLI; confine RenderingGen/PipelineGen.
**Owner:** maintainer del percorso overlay Chronon3d + maintainer throughput PipelineGen.
**Dipende da:** niente per P0/P1. P2–P4 dipendono da P0/P1. P5–P7 dipendono dai gate misurati.
**Non sostituisce:** [`TICKET-PERF-FRAME-PROOF-CHEAP`](TICKET-PERF-FRAME-PROOF-CHEAP.md),
[`TICKET-PERF-TIMELINE-FLATBUFFER`](TICKET-PERF-TIMELINE-FLATBUFFER.md),
[`TICKET-PERF-REVOLUTIONARY-IDEAS`](TICKET-PERF-REVOLUTIONARY-IDEAS.md),
[`TICKET-100X-PERF-WAVES`](TICKET-100X-PERF-WAVES.md),
[`TICKET-MAX-THROUGHPUT-OVERLAY-ARCHITECTURE`](../../../refactored/docs/operations/TICKET-MAX-THROUGHPUT-OVERLAY-ARCHITECTURE.md).
Questo ticket dice **in che ordine** e **con quale evidenza** quei lavori si eseguono; non duplica le loro specifiche.
**Inventario demolizioni:** §12 (2026-11-03) — assurdità correnti, cosa eliminare, ordine d'attacco e tetto risultante.

---

## 0. Regola di evidenza (vincolante)

Ogni affermazione in questo ticket deve avere uno di questi tag. Una frase senza tag non è un requisito, è una nota.

| Tag | Significato |
|---|---|
| `[CODICE]` | Verificato leggendo il sorgente in questo checkout; il path e la funzione sono citati. |
| `[MISURA]` | Letto da un artefatto nominato (job JSON, sidecar, report); il valore è riportato senza arrotondamenti inventati. |
| `[INFERITO]` | Conseguenza logica di codice + dati, ma **non** ancora osservata da un runtime marker. Va promossa a `[MISURA]` dal passo che la verifica. |
| `[IPOTESI]` | Idee o stime di altri documenti; **non** sono accettazioni e non autorizzano claim. |
| `[NOT RUN]` | Non eseguito in questo checkout. |

Regole derivate:

1. **Nessuna stima percentuale come fatto.** I fattori 5–10×, 3–10×, 100× ecc. restano `[IPOTESI]` finché non hanno un before/after sullo stesso payload e sullo stesso SHA.
2. **`timeline_eval_ms = 0` non è una misura di costo zero.** Se il campo non è applicabile, deve essere `null`, non `0.0`.
3. **Un artifact senza SHA/comando/data non è evidenza.** I path degli artefatti sono citati sotto.
4. **Il gate decide, non il ticket.** Se un gate non passa, la fase resta `FAIL`/`NOT RUN`; non si apre la fase successiva per nasconderla.
5. **Il pair va dichiarato.** Un PASS su working tree dirty non è same-SHA; va registrato il digest del diff.

---

## 1. Definizione di "100x" e tetto fisico

"100x" non è una grandezza unica. Questo ticket ne distingue due, perché hanno tetti fisici diversi.

### 1.1 100x sul throughput overlay/min

- `[MISURA]` Baseline certificata del gate: **77,32 item/min** di mediana su tre run closeout del 30-10 (65–70 item, v2.1 senza mappe); il picco registrato è **101,71/min** su 70 item (`gate-100-r4`). Fonte: [`refactored.md`](../../../docs/stato/refactored.md) §1 e [`TICKET-MAX-THROUGHPUT-OVERLAY-ARCHITECTURE`](../../../refactored/docs/operations/TICKET-MAX-THROUGHPUT-OVERLAY-ARCHITECTURE.md) §9.10.
- `[ARITMETICA]` 100× di 77,32/min = **7.732/min**.
- `[MISURA]` Un daemon Chronon isolato è stato misurato a **~250–270 overlay/min**; il pool di tre daemon a **~750–800/min**. Fonte: [`TICKET-MAX-THROUGHPUT-OVERLAY-ARCHITECTURE`](../../../refactored/docs/operations/TICKET-MAX-THROUGHPUT-OVERLAY-ARCHITECTURE.md) §1.4. Misura registrata, **non rieseguita in questo checkout**.
- `[ARITMETICA]` 7.732/min ÷ ~267/min per daemon ≈ **29 daemon**. Anche prendendo il limite superiore 800/min per 3 daemon, servirebbero ~29 processi.
- `[MISURA]` Il censimento host del 2026-10-27 registra **16.376 MiB** di VRAM totali, **~1.450 MiB** per daemon e **5.156 MiB** occupati da Ollama. Dodici daemon richiederebbero >17,4 GB, oltre la VRAM disponibile. Fonte: stesso ticket §2.1–2.2. Misura registrata, **non rieseguita**.
- **Conseguenza `[INFERITO]`:** 100× via moltiplicazione dei daemon è **fisicamente escluso** su questo host. Non è un problema di scheduling; è VRAM. Un 100× può esistere solo se la maggior parte degli item **non viene renderizzata** (cache, stream-copy, EDL), non se viene renderizzata 100× più in fretta.

### 1.2 100x sul tempo per frame

- `[MISURA]` r49: `render_loop_wall_ms = 74.257,815969` su **1.607 frame** = **46,21 ms/frame** (21,6 fps). Fonte: `ops/jobs/garlasco_editorial_e2e_20261030/job-full-r49.json`, `overlay_render.batches[0].artifact.chronon_telemetry.job`.
- `[ARITMETICA]` 100× = **0,462 ms/frame** = **2.164 fps**.
- `[MISURA]` NVENC sulla RTX A4000: **244,97 fps** al preset default `slow`, **408,4/422,1 fps** a `p4`, **714,6 fps** a `p2`. Fonte: [`TICKET-100X-PERF-WAVES`](TICKET-100X-PERF-WAVES.md), misure 2026-11-02. Misura registrata, **non rieseguita**.
- **Conseguenza `[INFERITO]`:** anche il solo encode a `p2` è ~3× sotto i 2.164 fps richiesti dal 100× per-frame. Il 100× per-frame **non è raggiungibile** su una singola A4000 con questo artefatto. Un 100× può esistere solo cambiando l'artefatto: frame riusati, stream-copy, cache, o player EDL che non produce MP4.

### 1.3 Cosa questo ticket promette, e cosa no

- **Promette:** rendere misurabili i due percorsi, eliminare il costo di valutazione dai frame duplicati sul percorso dinamico, rendere cheap la valutazione dei frame veri, e portare il gate esistente a 100/min con evidenza ripetibile.
- **Non promette:** un fattore 100× come accettazione. Il 100× è un obiettivo di ricerca che richiede una decisione di prodotto sull'artefatto (cache/stream-copy/EDL), non una patch del renderer. La decisione è P7.
- **Non promette:** che il percorso `compiled_program` di r101/r102 sia già veloce. Oggi quel percorso **non è misurato** (P0/P4).

---

## 2. Evidenza: r49 non è r101/r102

### 2.1 Confronto artifact

| Campo | r49 | r101 | r102 |
|---|---:|---:|---:|
| `execution_reason` | `required_gpu_native_full_graph` | `gpu_composition_pipe_no_video_source` | `gpu_composition_pipe_no_video_source` |
| `surface_handoff_path` | `direct` | `host_upload` | `host_upload` |
| Batch | 1 | 9 | 9 |
| `timeline_eval_ms` | **68.306,496** | **0,0** (128 campi) | **0,0** (128 campi) |
| `render_loop_wall_ms` somma batch | 74.257,815969 | 10.604,858423 | 10.274,542492 |
| `job_wall_ms` somma batch | 76.001,842432 | 15.525,875086 | 16.374,574498 |
| `vulkan_frames` / `nvenc_frames` (primo batch) | 763 / 763 | 2 / 2 | 2 / 2 |
| `fast_path_reused_frames` (somma batch) | 844 | **1.584** | **1.584** |
| `graph_executed_frames` (somma batch) | 1.526 | **44** | **44** |
| `glyph_atlas_upload_ms` (primo batch) | 125,954 | 0,0 | 0,0 |
| `text_draw_ms` (primo batch) | 94,189 | 0,0 | 0,0 |
| `gpu_execute_ms` (primo batch) | 3.027,389 | 2,885 | 5,086 |
| `gpu_utilization_avg` (top-level) | 6,593959731543624 | 4,5 | 1,0 |
| `vram_used_peak_mb` (top-level) | 7.616 | 7.748 | 7.748 |

Fonti: `ops/jobs/garlasco_editorial_e2e_20261030/job-full-r49.json`, `job-full-r101.json`, `job-full-r102.json`; i valori sono letti da `job.result.result.overlay_render.batches[*].artifact.chronon_telemetry.job`. Le somme sono sui 9 batch di r101/r102 e sull'unico batch di r49.

### 2.2 I 128 campi non sono 128 misure

`[MISURA]` r101/r102 hanno 24 artifact item (22 con telemetria) + 9 artifact batch + 1 artifact top-level. Ogni telemetria compare due volte nella risposta (`job.result.result` e `result.result`) e due volte per nodo (`job.cpu_breakdown` e `summary.first_frame.render`): 32 nodi × 2 blocchi × 2 copie = **128 campi** `timeline_eval_ms`. Gli item non sono misure indipendenti: in r101 l'item 1 ripete `render_loop_wall_ms = 579.627139` del batch 0, mentre gli item 2–3 ripetono `2536.193288` del batch 1. Le misure reali sono **9**, una per batch; il top-level è una copia del primo batch.

### 2.3 Perché `timeline_eval_ms` è 0: la catena verificata

1. `[CODICE]` Il contatore `timeline_eval_wall_us` viene incrementato **solo** in `CompositionFrameState::render_single_frame()`, `Chronon3d/src/render_graph/pipeline/composition_frame_single.cpp:61-70`, dentro `if (sw_sidecar && sw_sidecar->counters())`.
2. `[CODICE]` Il percorso `RenderPipeline::render_program` (`Chronon3d/src/runtime/render_pipeline.cpp`) usa `compiler::TypedProgramSampler{}.sample_into(...)` e `SceneMaterializer`, e **non** chiama `render_compiled_composition_frame_temporal` né incrementa `timeline_eval_wall_us`.
3. `[CODICE]` La scelta è esplicita in `Chronon3d/apps/chronon3d_cli/commands/video/common/pipe_export_stages.cpp:106-118`:
   - se `ctx.program` è non nullo → `pipeline.render_program(*ctx.program, ...)`;
   - altrimenti → `graph::render_compiled_composition_frame_temporal(..., ctx.sw_renderer, ...)`.
4. `[CODICE]` `ctx.sw_renderer` è `session.renderer_ptr()`, usato nello stesso file e nello stesso loop per `backend()` e `node_cache()`; non è nullo quando il renderer esiste.
5. `[CODICE]` `ctx.counters` è `session.renderer_ptr()->counters()` (`pipe_export_profile.cpp:11-17`); se fosse nullo, anche `graph_execute_ms` sarebbe 0. In r101/r102 `graph_execute_ms` è **5,305 ms** / **18,738 ms**, quindi i counter esistono.
6. `[INFERITO]` Se r101/r102 avessero preso il ramo dinamico (`ctx.program == nullptr`), `sw_sidecar` sarebbe stato non nullo e `render_single_frame` avrebbe incrementato `timeline_eval_wall_us` almeno per i 44 frame eseguiti dal grafo. Il contatore è invece esattamente 0 su tutti i batch. L'unica spiegazione compatibile è che abbiano preso il ramo `render_program`.
7. `[CODICE]` Non è il ramo `template_scene`: `src/timeline/compile_evaluate.cpp:91-93` assegna `execution_mode = SceneExecutionMode::DynamicCallback` e `is_static_topology = false` in modo incondizionato. Non esiste un produttore che li metta a `StaticTopologySlots`/`true` (`code_search` su `Chronon3d`: solo il restore della cache in `src/cache/compiled_artifact_cache.cpp:363`).
8. `[CODICE]` `CompiledFrameGraph::prepared_parameters` è dichiarata in `include/chronon3d/render_graph/compiler/compiled_frame_graph.hpp:295`, ma **nessun produttore** in `src/` o `apps/` la popola: i soli assegnamenti sono nei test (`tests/render_graph/compiler/test_template_program.cpp:96`, `tests/render_graph/pipeline/test_frame_delta_compiler.cpp:47`) e nel supporto di serializzazione. La tavola per-frame esiste (`include/chronon3d/render_graph/pipeline/frame_parameter_table.hpp`), ma non è collegata al compilatore di produzione.
9. `[MISURA]` `identity_fallback_frames` è **presente e vale 0** nei job JSON r101/r102 (22 occorrenze, sotto `job.gpu`), non è assente. `[CODICE]` Il codice attuale lo incrementa solo in `render_single_frame` (`render_counter_macros.hpp`), non in `render_program`: il valore 0 è compatibile con il ramo compilato, ma **non lo prova** e non va usato come discriminante. Il commit `cd593ffc3` (2026-11-03 06:57:33) è successivo a r101/r102; la cronologia del commit, da sola, non spiega il campo già presente.

**Conseguenza `[INFERITO]`:** r101/r102 non misurano il costo del timeline; misurano un percorso in cui il costo non viene emesso. Non sono un before/after di r49 e non autorizzano né un guadagno né una regressione.

### 2.4 Cosa significa per i ticket esistenti

- `TICKET-PERF-FRAME-PROOF-CHEAP`: il costo r49 di 42,5 ms/frame resta valido **solo per il percorso dinamico**. Il gate sui duplicati (43,5 → ≤2 ms) va misurato su quel percorso, non su r101/r102.
- `TICKET-PERF-TIMELINE-FLATBUFFER`: il gate `timeline_eval ≤ 3 ms/frame` non è valutabile sul percorso `render_program`, perché il campo è assente. Va misurato sul percorso dinamico dopo P1, oppure sostituito da `program_sample_ms` dopo P0/P4.
- `TICKET-MAX-THROUGHPUT-OVERLAY-ARCHITECTURE`: la frase "timeline_eval=0 nei run correnti" va letta come "campo non applicabile", non come "costo eliminato". La distinzione va riportata nel ticket al prossimo aggiornamento.

---

## 3. Claim ledger

| ID | Affermazione | Tag | Evidenza |
|---|---|---|---|
| C01 | r49 ha `timeline_eval = 68.306,496 ms` su 1.607 frame (42,5 ms/frame). | `[MISURA]` | `job-full-r49.json`, `batches[0].artifact.chronon_telemetry.job.cpu_breakdown.timeline_eval_ms`; 1.607 = `gpu.vulkan_frames` 763 + `cpu_breakdown.fast_path_reused_frames` 844. |
| C02 | r49 ha 763 frame resi e 844 duplicati. | `[MISURA]` | Stesso job: `job.gpu.vulkan_frames=763`, `job.gpu.nvenc_frames=763`, `cpu_breakdown.fast_path_reused_frames=844`. |
| C03 | r49 ha GPU `3.027,389 ms`, utilizzazione media 6,593959731543624%, VRAM 7.616 MB. | `[MISURA]` | Stesso job: `job.gpu.gpu_execute_ms`, `job.hardware.*`. |
| C04 | r101/r102 hanno 9 batch, 44 frame eseguiti dal grafo, 1.584 fast-path, `timeline_eval=0` in 128 campi che corrispondono a 9 misure reali. | `[MISURA]` | `job-full-r101.json`/`r102.json`, somme su `overlay_render.batches[*]`; conteggio dei campi in §2.2. |
| C05 | r101/r102 hanno `render_loop_wall_ms` 10.604,858423/10.274,542492 ms e `job_wall_ms` 15.525,875086/16.374,574498 ms. | `[MISURA]` | Stessi job, somma dei 9 batch. |
| C06 | Il contatore `timeline_eval_wall_us` esiste solo in `render_single_frame`. | `[CODICE]` | `composition_frame_single.cpp:61-70`; `code_search` su `Chronon3d`. |
| C07 | `render_program` non incrementa il contatore. | `[CODICE]` | `src/runtime/render_pipeline.cpp`, funzione `render_program`; nessun riferimento a `timeline_eval_wall_us` nel file. |
| C08 | Il ramo è scelto da `ctx.program` in `pipe_export_stages.cpp`. | `[CODICE]` | `pipe_export_stages.cpp:106-118`. |
| C09 | r101/r102 hanno preso il ramo `render_program`. | `[INFERITO]` | Catena C06–C08 + `ctx.sw_renderer`/`ctx.counters` non nulli + 0 esatto; da confermare con `evaluation_mode` in P0. |
| C10 | `execution_mode` è hardcoded a `DynamicCallback` e `is_static_topology` a `false`. | `[CODICE]` | `src/timeline/compile_evaluate.cpp:91-93`; restore cache in `compiled_artifact_cache.cpp:363`. |
| C11 | `prepared_parameters` non ha produttori di produzione. | `[CODICE]` | `code_search` su `Chronon3d`: solo test e supporto di serializzazione. |
| C12 | `identity_fallback_frames` è presente e vale 0 in r101/r102; il codice attuale lo incrementa solo in `render_single_frame`. | `[MISURA]` + `[CODICE]` | JSON r101/r102, 22 occorrenze sotto `job.gpu`; `render_counter_macros.hpp` e `composition_frame_single.cpp`. |
| C13 | 3 daemon rendono ~750–800 overlay/min; 1 daemon ~250–270. | `[MISURA]` registrata | `TICKET-MAX-THROUGHPUT-OVERLAY-ARCHITECTURE` §1.4; non rieseguita qui. |
| C14 | 12 daemon non stanno nella VRAM con Ollama. | `[MISURA]` registrata | Stesso ticket §2.1–2.2: 16.376 MiB, ~1.450 MiB/daemon, 5.156 MiB Ollama. |
| C15 | Il gate 100/min è FAIL/non certificato; mediana 77,32/min. | `[MISURA]` registrata | `docs/stato/refactored.md` §1; `TICKET-MAX-THROUGHPUT` §9.10. |
| C16 | Il packed cross-scene ha reso 43,73 item/min ed è stato riportato opt-in. | `[MISURA]` registrata | `docs/stato/refactored.md` §1; `TICKET-MAX-THROUGHPUT` §11.6. |
| C17 | NVENC: 244,97 fps `slow`, 408,4/422,1 `p4`, 714,6 `p2`. | `[MISURA]` registrata | `TICKET-100X-PERF-WAVES`, aggiornamento 2026-11-02. |
| C18 | DirectYUV esiste ma non è default/certificato end-to-end. | `[IPOTESI/STATO DICHIARATO]` | `100x-ideas.md` §1 e `TICKET-100X-PERF-WAVES`; da verificare con i 15 gate zero-copy prima di usarlo come leva. |
| C19 | La cache overlay è installata ma spenta (`not_applicable`, lookup 0–1). | `[IPOTESI/DA VERIFICARE]` | Audit N7 in `100x-IDEE-NUOVE-2026-10-31.md`; il wiring va riletto in `refactored/internal/app/wiring/script_generation_render_cache.go` prima di agire. |
| C20 | 100× via daemon o via render+encode è fisicamente escluso su questo host. | `[ARITMETICA]` da C01/C13/C14/C17 | §1.1–1.2. |

---

## 4. Piano di esecuzione

### P0 — Rendere misurabile il percorso compilato (BLOCKING)

**Obiettivo:** eliminare l'ambiguità `0.0` vs "non misurato" e dare un numero al percorso `render_program`. Senza P0, P2/P3 possono ottimizzare il percorso sbagliato.

**Passi:**

1. `[CODICE]` Aggiungere `evaluation_mode` al blocco `job` del sidecar:
   - `compiled_composition` quando `ctx.program == nullptr` (ramo `render_compiled_composition_frame_temporal`);
   - `compiled_program` quando `ctx.program != nullptr` (ramo `render_program`);
   - `direct_yuv` sul percorso DirectYUV, che non passa né da `render_program` né dalla valutazione della composizione.
   Il valore va impostato nel setup dove la lane e il programma sono noti, **non** dedotto da `execution_reason`: r101/r102 dimostrano che `gpu_composition_pipe_no_video_source` non distingue i due rami.
2. `[CODICE]` Rendere `timeline_eval_ms` nullable. Su `compiled_program` e `direct_yuv` deve essere `null`, mai `0.0`. Su `compiled_composition` resta il numero misurato da `render_single_frame`.
3. `[CODICE]` Aggiungere `program_sample_ms`, misurato intorno a `TypedProgramSampler::sample_into` in `RenderPipeline::render_program`:
   - nuovo counter `program_sample_wall_us` in `include/chronon3d/core/profiling/render_counter_macros.hpp`;
   - campionamento in `apps/chronon3d_cli/commands/video/common/pipe_export_profile.cpp` (`sample_frame_profile`/`project_frame_timings`);
   - esposizione in `pipe_timing_sidecar_frames.cpp` e `pipe_timing_sidecar_summary.cpp`.
4. `[CODICE]` Aggiungere `scene_materialize_ms`, `scene_materialize_count` e `scene_materialize_cache_hits` al blocco `prepare`; misurare solo il ramo di miss dentro `m_topologies.compute_if_absent(...)` in `render_pipeline.cpp`; su cache hit il tempo non è applicabile e va registrato `null`, non `0`. Nel dettaglio frame, esporre `scene_materialize_cache_hit` booleano.
5. `[CODICE]` Aggiornare lo schema test `tests/cli/test_timing_sidecar_schema.cpp` per accettare:
   - `evaluation_mode` obbligatorio nelle nuove versioni;
   - `timeline_eval_ms` numerico **oppure** `null`;
   - `program_sample_ms` numerico **oppure** `null`;
   - `scene_materialize_ms` numerico **oppure** `null`.
6. `[CODICE]` Aggiornare il consumatore del sidecar in `RenderingGen/renderinggen/internal/chronon/telemetry.go`: `BoundTimingSidecar` oggi conserva il blocco `job` senza validarne i campi, e `TelemetryMetrics` proietta solo una lista esplicita di chiavi numeriche. Aggiungere qui la validazione: `compiled_program` richiede `timeline_eval_ms` nullo e `program_sample_ms` numerico; `compiled_composition` richiede `timeline_eval_ms` numerico; `direct_yuv` richiede `timeline_eval_ms` e `program_sample_ms` nulli/assenti. Il gate di schema lato CLI è in `Chronon3d/tests/cli/test_timing_sidecar_schema.cpp`. `receipt_verify.go` è la policy di verifica dell'output, non il parser del timing: non è il punto in cui aggiungere questo controllo.

**Test:**

- Unit: un job con `program != nullptr` produce `evaluation_mode=compiled_program`, `timeline_eval_ms=null`, `program_sample_ms>0`.
- Unit: un job con `program == nullptr` produce `evaluation_mode=compiled_composition`, `timeline_eval_ms>0`.
- Unit: DirectYUV produce `evaluation_mode=direct_yuv` e lascia nulli i tempi degli evaluator non eseguiti.
- Schema: entrambe le forme passano; un `timeline_eval_ms=0.0` con `evaluation_mode=compiled_program` viene rifiutato dal validator.
- Integrazione: rieseguire un batch r101-like e uno r49-like; l'MP4 e l'overlay SHA devono essere **identici** prima/dopo la sola strumentazione.

**Gate P0:**

| Gate | Evidenza richiesta |
|---|---|
| G0.1 | Sidecar r101-like: `evaluation_mode=compiled_program`, `timeline_eval_ms=null`, `program_sample_ms>0`. |
| G0.2 | Sidecar r49-like: `evaluation_mode=compiled_composition`, `timeline_eval_ms>0`. |
| G0.3 | `tests/cli/test_timing_sidecar_schema` PASS. |
| G0.4 | MP4/overlay SHA invariant rispetto al run pre-patch. |

**Stato:** `[CODICE]` campi/contatori/serializer e validazione RenderingGen implementati; `[MISURA]` i run r103–r105 e P4 confermano `evaluation_mode=compiled_program`, `timeline_eval_ms=null`, e `program_sample_ms` rispettivamente 0,423/0,513/0,927/0,391 ms. `[MISURA]` è verificato G0.1 per il percorso r101-like. `[NOT RUN]` G0.2 (run `compiled_composition` r49-like), G0.3 (test C++ dello schema) e G0.4 (confronto SHA pre/post strumentazione). Il test Go `./internal/chronon` risulta PASS dal run precedente; non è stato rieseguito in questa sessione.

**Rischio:** cambiare schema e validator può rompere artifact vecchi. Mitigazione: campi additivi e fallback compatibile per sidecar storici senza `evaluation_mode`; il fallback non emette warning. Il campo presente ma `null` è un errore. Demolition debt sotto.

---

### P1 — Re-baseline r49 sul pair corrente (BLOCKING)

**Obiettivo:** avere un numero r49 aggiornato sul pair corrente, non sul pair dichiarato nei documenti.

**Stato del checkout `[CODICE]` (riletto 2026-11-03 dopo il commit di P0):**

- `Chronon3d` HEAD `b38f7e8f9`, working tree **pulito** (`git status --porcelain` vuoto).
- `ChrononMotion3D` HEAD `83af962`, working tree con **1 file modificato**.
- P0 è quindi su commit, non più solo nel working tree. La precondition same-SHA di G1.3 è soddisfatta per C3D; per CM3D resta il digest del diff (1 file).
- Quindi il pair **non è clean** in questo workspace. Un PASS qui è "dirty-tree, con digest del diff", non same-SHA. Va registrato `git status --porcelain` + `git diff | sha256sum` per entrambi.

**Passi:**

1. Decidere se usare un worktree dedicato pulito o registrare il digest del diff. Non dichiarare same-SHA finché il tree non è pulito.
2. Ripetere il payload r49 con `required_gpu_native_full_graph`, `ctx.program = nullptr`, per **3 run** (es. `r103`, `r104`, `r105`), stesso payload e stesso host quiescente.
3. Salvare per ogni run: `job-full-rNNN.json`, sidecar, SHA dell'overlay, SHA dell'MP4, `timeline_eval_ms`, `render_loop_wall_ms`, `vulkan_frames`, `nvenc_frames`, `fast_path_reused_frames`, `graph_executed_frames`.
4. Confrontare la split 763/844: se differisce, **fermarsi** e attribuire la differenza (payload, cache, dirty tree, versione binario) prima di usare il numero.
5. Registrare mediana e dispersione su 3 run; nessuna singola run è una baseline.

**Gate P1:**

| Gate | Evidenza richiesta |
|---|---|
| G1.1 | 3/3 run SUCCEEDED, stesso payload, stesso SHA overlay. |
| G1.2 | `timeline_eval_ms > 0`, con split frame resi/duplicati riprodotta o differenza attribuita. |
| G1.3 | `git status`/`git diff` digest registrato per C3D e CM3D. |
| G1.4 | Evidenza in `ops/jobs/garlasco_editorial_e2e_20261030/verified-baseline-r103-r105/`. |

**Stato:** `[MISURA]` eseguiti 3/3 job sullo stesso worker sano. G1.1 **FAIL**: gli SHA overlay sono `ede9d15758a0c7e06abd8fa46a20a51b54e75ce334b55f98de527fb36d8413e9`, `480171f48d73333a9394e8f41ae26ff087895df9945e5300717101808a72434c` e `cfc7b2a7623a0b819d0d56e77ec870155c92ef6c04035d9aa05cf164c2b61e19`. `[MISURA]` il digest delle `scenes` è identico nei tre job, ma i digest di `overlay_plan` e `semantic_render_bundle` differiscono (valori per run nel manifest): la variabilità si introduce dopo la generazione delle scene e invalida il confronto byte-identico dell'overlay. G1.2 **FAIL/non valutabile come baseline r49**: tutti i sidecar riportano `compiled_program` e `timeline_eval_ms=null`, non `compiled_composition` con timing positivo; la split 763/844 non è quindi stata validata. G1.3: `[MISURA]` pair dirty, con modifiche locali in Chronon3d e ChrononMotion3D; i digest completi non sono registrati in questi run. G1.4 **PASS**: payload, manifest di esecuzione e riepilogo evidenze sono in `ops/jobs/garlasco_editorial_e2e_20261030/verified-baseline-r103-r105/`. Il contenuto canonico del payload è uguale a r49 (`83fc04df…aace9`), ma il runtime seleziona un percorso diverso: indagare la compilazione/selezione della lane prima di usare questi numeri come baseline r49.

`[INFERITO]` Per una baseline P1 confrontabile occorre fissare anche l'output `overlay_plan`/`semantic_render_bundle` e rilanciare la sola fase di render, oppure spiegare perché i loro digest cambiano mantenendo uguali le `scenes`; rigenerare solo la richiesta di generazione non garantisce lo stesso artefatto.

**Incidenti operativi `[MISURA]`:** i job sono stati instradati al worker sano `host_57_129_132_133`; è stato rimosso l'override Master che consentiva il claim prima del completamento PREPARE/FINALIZE. Il primo tentativo r105 è fallito in staging per spazio disco insufficiente; il retry `job_1793700958377067719_e2ce4b15` è riuscito dopo il recupero dello spazio. Tutti i job finali risultano `SUCCEEDED`; il manifest conserva gli ID e i timing remoti.

---

### P2 — Frame-proof: decidere prima di valutare

**Obiettivo:** togliere la valutazione dal percorso dei frame duplicati sul percorso dinamico. Questo è il primo salto misurabile, non il 100×.

**Fatto di partenza `[CODICE]`:**

- `execution_mode` è sempre `DynamicCallback` (`compile_evaluate.cpp:91`).
- `prepared_parameters` non ha produttori (`compiled_frame_graph.hpp:295`).
- `render_single_frame` valuta **prima** che `TileExecutionPolicy` possa decidere il duplicato (`composition_frame_single.cpp:35-52`).
- `FrameParameterTable` e `FrameParameterSampler` esistono già (`frame_parameter_table.hpp`), ma non sono collegati al compilatore di produzione.

**Passi:**

1. Nel compilatore dell'item, produrre una `FrameParameterTable` con una riga POD per frame, usando `FrameParameterSampler::prepare`/`FrameParameterTable::sample`; la tabella deve contenere **tutti** i campi che entrano nel fingerprint della scena (transform, opacity, paint, visibility, resource, text run, effect parameters).
2. Impostare `execution_mode = SceneExecutionMode::StaticTopologySlots` per gli item eleggibili e produrre `template_scene` una volta sola. L'eleggibilità va decisa dal compilatore, non a runtime.
3. In `render_single_frame`, usare la tabella/`template_scene` quando disponibili e decidere il duplicato con un lookup O(1) **prima** di `evaluate_scene`.
4. Incrementare `identity_fallback_frames` solo quando l'identità a monte manca; il gate richiede zero su un batch certificato.
5. Cancellare il doppio calcolo (`duplicate_frame_eligible` raggiungibile dopo la valutazione) una volta che il nuovo percorso è verde.

**Gate P2 (dal ticket esistente, non rinegoziato):**

| Gate | Evidenza richiesta |
|---|---|
| G2.1 | Costo medio dei 844 duplicati da **43,5 ms a ≤2 ms**. |
| G2.2 | `render_loop_wall_ms` del batch r49 da **74,3 s a ≤45 s**. |
| G2.3 | MP4 **byte-identico** (SHA-256) al controllo. |
| G2.4 | `identity_fallback_frames = 0` nel batch certificato; sidecar ×3. |

**Stato:** `[NOT RUN]`. Dipende da P0 e P1.

**Rischio:** un digest incompleto produce un falso duplicato. Il gate byte-identico è l'unica prova ammessa; nessun "sembra uguale".

---

### P3 — Timeline flatbuffer: rendere cheap i frame veri

**Obiettivo:** dopo P2, ridurre il costo dei frame realmente renderizzati, precompilando per item una tavola piatta di parametri già risolti.

**Passi:**

1. Precompilare per ogni frame dello item una riga POD con transform/opacity/parametri già risolti, così il costo per frame è un indice, non una valutazione.
2. Usare la stessa identità della tabella di P2 (una sola authority; niente seconda tabella).
3. Misurare `timeline_eval_ms` sul percorso dinamico; sul percorso `compiled_program` usare `program_sample_ms`.
4. Misurare `loop_graph_eval` prima/dopo; compilare le espressioni una volta (C-10) se il profilo mostra ri-parse.

**Gate P3 (dal ticket esistente):**

| Gate | Evidenza richiesta |
|---|---|
| G3.1 | `timeline_eval ≤ 3 ms/frame` sul corpus overlay ricostruito da r49. |
| G3.2 | MP4 **byte-identico**. |
| G3.3 | Nessun frame oltre il budget 41,67 ms. |
| G3.4 | Profilo per-frame attribuito al 100%, residuo >10% assente. |

**Stima, non gate `[IPOTESI/ARITMETICA]`:** 763 frame resi × ~14 ms + 844 duplicati × ~0 ≈ **13 s** di batch; ~500 overlay/min su ~5 lane. Questi numeri sono un'ipotesi aritmetica del ticket esistente, non un risultato. Non citarli come accettazione.

**Stato:** `[NOT RUN]`. Dipende da P2.

---

### P4 — Misurare il percorso `render_program` (parallelo a P2/P3)

**Obiettivo:** sapere se r101/r102 usano già un percorso cheap o se il loro costo è altrove. Senza P4, il piano può ottimizzare il percorso dinamico mentre la produzione usa quello compilato.

**Passi:**

1. Con P0 attivo, eseguire un batch r101-like e raccogliere `program_sample_ms`, `scene_materialize_ms`/cache hit, `graph_execute_ms`, `gpu_execute_ms`, `probe`, `prepare`, `encode`.
2. Attribuire il `job_wall_ms` al 100%: ogni voce deve avere un tempo o `null`; il residuo non attribuito deve essere ≤10%.
3. Se domina `program_sample_ms`, aprire un intervento su `TypedProgramSampler` (misurare prima/dopo, MP4 byte-identico).
4. Se dominano `probe`/`prepare`/`startup` per batch, l'intervento è sul batching/startup, non sul renderer.
5. Se il percorso è già cheap, registrarlo: il target P3 non si applica a quel percorso e va misurato con `program_sample_ms`.

**Gate P4:**

| Gate | Evidenza richiesta |
|---|---|
| G4.1 | `evaluation_mode=compiled_program` su tutti i batch del job r101-like. |
| G4.2 | `program_sample_ms` presente e >0; `timeline_eval_ms=null`. |
| G4.3 | Attribuzione del wall ≥90%; residuo ≤10% o spiegato. |
| G4.4 | MP4/overlay SHA invariato rispetto al run pre-P0. |

**Stato:** `[MISURA]` job r101-like `job_1793701186583939610_8ec8b898` riuscito; il sidecar attribuisce il percorso a `compiled_program`, con `timeline_eval_ms=null`, `program_sample_ms=0,391 ms`, `scene_materialize_ms=15.418,692 ms`, 72 materializzazioni e 0 cache hit. L'overlay SHA è `c5eab14a2f6cf28285614aa83ee524159fa1a796057443464a9919642bc94b6e`. G4.1/G4.2 **PASS**. G4.3 **NOT PROVEN**: il wall non è attribuito per ≥90% dalle categorie richieste; `job_wall_ms=16.465,636` e `render_loop_wall_ms=15.596,397` sono riportati senza dichiarare chiuso il residuo. G4.4 **NOT RUN**: manca un SHA pre-P0 confrontabile. La medesima evidenza per i quattro run è nel `run-manifest.json` accanto ai payload.

---

### P5 — Gate 100 overlay/min (esistente)

**Obiettivo:** chiudere il gate già dichiarato, senza confonderlo con il 100×.

**Stato `[MISURA]` registrata:**

- Mediana 77,32/min su tre run closeout 30-10 (65–70 item, senza mappe).
- Picco 101,71/min su 70 item (`gate-100-r4`).
- Con mappe: 83,98/min di mediana (serie 25-10).
- Packed cross-scene: 43,73/min, FAIL; tornato opt-in.
- Mix 100 item con mappe: **NOT RUN**.

**Gate P5:**

| Gate | Evidenza richiesta |
|---|---|
| G5.1 | 100/100 item materializzati, mappe incluse. |
| G5.2 | 3 run, mediana ≥100 item/min, dispersione riportata. |
| G5.3 | 0 retry, 0 errori DTS, stessi SHA artefatto. |
| G5.4 | Stesso payload immutabile e host quiescente. |

**Stato:** `[FAIL/NOT RUN]` secondo la documentazione; da rieseguire.

---

### P6 — Leve oltre 100/min: solo dopo P2–P5

Queste leve non sono accettazioni finché non hanno un gate. L'ordine è per rapporto evidenza/sforzo, non per fattore dichiarato.

| # | Leva | Base dichiarata (non verificata in questo checkout salvo dove taggato) | Gate minimo | Stato |
|---|---|---|---|---|
| P6.1 | Attivare la cache overlay | `[IPOTESI/DA VERIFICARE]` Audit N7: cache installata ma spenta, `not_applicable`, lookup 0–1; 65–76 s di re-render identico per run. | Run ripetuta: cache hit >0, overlay SHA identico, tempo overlay in discesa, 3 run. | `[NOT RUN]` |
| P6.2 | Cache di render per layer | `[IPOTESI/DA VERIFICARE]` L1/L2/L3 e cache per item dichiarate; granularità per layer assente. | Modifica di una parola: ri-render della sola finestra; SHA; ADR per la nuova chiave. | `[NOT RUN]` |
| P6.3 | Zero-copy NV12 come unico path | `[IPOTESI/DA VERIFICARE]` DirectYUV/Z-waves dichiarati; lane non certificata/default. | 15 gate di [`docs/ZERO_COPY_GATES.md`](../ZERO_COPY_GATES.md); `rgba_to_nv12_frames=0`; parità frame; nessuna regressione fps. | `[NOT RUN]` |
| P6.4 | Segment-parallel / stream-copy | `[MISURA]` packed superbatch 43,73/min FAIL; `[IPOTESI/DA VERIFICARE]` multi-socket dichiarato, splitter automatico assente. | Splitter con GOP ai confini; concat packet-copy; parità frame; nessun re-encode; VRAM misurata. | `[NOT RUN]` |
| P6.5 | Bake once, composite many | `[CODICE]` `TemplateProgramCache` esiste in `Chronon3d/src/runtime/template_program_cache.cpp`; `[IPOTESI/DA VERIFICARE]` catalogo finito e bake offline assente. | Chiave di bake; asset content-addressed; parità visiva; VRAM budget. | `[NOT RUN]` |
| P6.6 | EDL player / proxy-first | `[IPOTESI/DA VERIFICARE]` contratti wire EDL-like dichiarati; player assente. | Riproduzione 1080p realtime; parità curve player/renderer; export stesso artefatto. | `[NOT RUN]` — richiede ticket/ADR separato |

**Regola:** P6.4 non si riprova nella forma "packed" finché non è spiegato il 43,73/min; P6.3 non diventa default finché i 15 gate non sono verdi; P6.1 non si attiva senza una prova che il wiring è vivo.

---

### P7 — Decisione sul 100× (dopo i gate, non prima)

Il 100× non è un gate di questo ticket. È una decisione con due sole strade fisicamente compatibili:

1. **100× di throughput:** richiede che la maggior parte degli item non venga renderizzata. La metrica non è "fps", è **quota di item serviti da cache/stream-copy**. Serve una baseline: quanti item su 100 sono identici a un run precedente? Oggi la cache overlay è spenta (P6.1), quindi la quota è ~0. Il 100× via daemon è escluso (C13/C14).
2. **100× di tempo di creazione:** richiede il player EDL/proxy-first. È un componente nuovo, con contratto e ADR propri; non si implementa in questo ticket. Il tempo di creazione può scendere senza che il render MP4 sia 100× più veloce.

**Decisione richiesta:** owner di prodotto + rendering-core. Senza una delle due, il "100×" resta `[IPOTESI]` e non va scritto in un report come risultato.

---

## 5. Dipendenze

```
P0 telemetria ──► P1 baseline r49 ──► P2 frame-proof ──► P3 timeline flatbuffer
      │                                      │
      └────────────► P4 program path ────────┘
                                             │
P5 gate 100/min ◄────────────────────────────┘
      │
      ▼
P6.1 cache overlay ──► P6.2 layer cache ──► P6.3 zero-copy ──► P6.4 segment/stream-copy
      │
      └────────────► P6.5 bake once ──► P6.6 EDL/proxy ──► P7 decisione 100×
```

P0 e P1 sono bloccanti. P2 e P4 possono partire in parallelo dopo P1. P3 non parte prima di P2. P6 non parte prima che P5 sia verde o che l'owner accetti esplicitamente un cambio di priorità.

---

## 6. Cosa non fare (anti-pattern verificati)

1. **Non aggiungere lane per alzare il rate.** L'audit del ticket timeline misura **25% dei campioni in spin TBB**; le lane si cannibalizzano. La terza lane ha già un costo per item maggiore (registrato nel ticket MAX-THROUGHPUT). `[MISURA]` registrata.
2. **Non usare il packed superbatch come leva.** Ha reso **43,73 item/min** contro 69,5–101,7 del controllo; è tornato opt-in. `[MISURA]` registrata.
3. **Non leggere `timeline_eval_ms=0` come "costo zero".** r101/r102 sono il controesempio: percorso diverso, campo non emesso. `[CODICE]`/`[INFERITO]`.
4. **Non dedurre il percorso da `execution_reason`.** r101/r102 hanno `gpu_composition_pipe_no_video_source` e prendono `render_program`; un job dinamico potrebbe avere lo stesso reason. Serve `evaluation_mode`. `[CODICE]`.
5. **Non usare il preset `p4` come prova di qualità.** B2 è accettato a `p4` per decisione dell'owner; gli SHA di output differiscono da `slow` e la qualità differenziale non è misurata. `[MISURA]` registrata.
6. **Non dichiarare same-SHA su un tree dirty.** Al 2026-11-03 C3D è pulito su `b38f7e8f9`, CM3D ha 1 file modificato su `83af962`; per quest'ultimo va registrato il digest del diff. `[CODICE]`.
7. **Non eseguire `ctest` su build stale.** Verificare mtime del binario e del sorgente (regola `AGENTS.md`).
8. **Non aggiungere una cache nuova senza ADR.** P6.2/P6.5 richiedono ADR e Demolition Debt.

---

## 7. Criteri di accettazione del ticket

Il ticket si chiude solo quando:

1. P0 è PASS: entrambi i percorsi hanno un modo esplicito e testato; `timeline_eval_ms` non è più ambiguo.
2. P1 è PASS: r49 è riprodotto su 3 run con pair/dirty-tree registrato.
3. P2 è PASS: duplicati ≤2 ms, wall ≤45 s, MP4 byte-identico, `identity_fallback_frames=0`.
4. P3 è PASS oppure è stato esplicitamente cancellato con una misura che mostra che il percorso dinamico non è più usato in produzione.
5. P4 è PASS: il percorso `render_program` è attribuito e misurato; `program_sample_ms` è nel sidecar.
6. P5 è PASS: 100/100 item, 3 run, mediana ≥100/min, mappe incluse.
7. P6.1–P6.5 hanno un gate misurato o restano `NOT RUN`; nessuna leva è dichiarata "fatta" senza before/after.
8. P7 ha una decisione scritta: 100× di throughput (cache/stream-copy) o 100× di creazione (EDL/proxy), oppure il 100× è ritirato come obiettivo.

Nessun punto si chiude con un exit code 0, una build verde o un artefatto caricato: serve il gate dichiarato.

---

## 8. Demolition Debt

| Voce | Owner | Reason | Exit condition | Equivalence gate | Removal scope | Status |
|---|---|---|---|---|---|---|
| Fallback "sidecar senza `evaluation_mode` accettato" | CLI/telemetry | Artifact vecchi (r49–r102) non hanno il campo | Tutti gli artifact di riferimento rigenerati con P0; nessun consumer legge il fallback | Schema test + un job vecchio e uno nuovo | Ramo di compatibilità in schema/validator, warning, test del fallback | `ACTIVE` (da introdurre in P0) |
| `timeline_eval_ms` numerico per entrambi i percorsi | CLI/telemetry | Transizione al campo nullable | Il percorso compilato pubblica `null` + `program_sample_ms` da almeno una release | Sidecar r101-like + r49-like | Vecchio default `0.0`, test che lo assume | `ACTIVE` (da introdurre in P0) |
| Eventuale cache layer/bake | rendering-core | Nuova cache (P6.2/P6.5) | ADR approvato e gate di parità | Parità pixel/visiva | Chiave, storage, contatori | `NOT STARTED` |

---

## 9. Evidenza e riferimenti

### Job e artifact

- `ops/jobs/garlasco_editorial_e2e_20261030/job-full-r49.json` — percorso dinamico, 68.306,496 ms di timeline, 763/844 frame.
- `ops/jobs/garlasco_editorial_e2e_20261030/job-full-r101.json` — 9 batch, 44/1.584, `timeline_eval_ms=0`, `execution_reason=gpu_composition_pipe_no_video_source`.
- `ops/jobs/garlasco_editorial_e2e_20261030/job-full-r102.json` — idem; overlay SHA identico a r101.
- `ops/jobs/garlasco_editorial_e2e_20261030/` — script e report della serie.

### Codice

- `Chronon3d/src/render_graph/pipeline/composition_frame_single.cpp:35-70` — eval e contatore timeline.
- `Chronon3d/src/runtime/render_pipeline.cpp` — `render_program`, `TypedProgramSampler`, `SceneMaterializer`.
- `Chronon3d/apps/chronon3d_cli/commands/video/common/pipe_export_stages.cpp:106-118` — scelta del ramo.
- `Chronon3d/apps/chronon3d_cli/commands/video/common/pipe_export_profile.cpp:11-100` — campionamento e proiezione dei tempi.
- `Chronon3d/apps/chronon3d_cli/commands/video/exporters/pipe_timing_sidecar_frames.cpp` — sezione per-frame.
- `Chronon3d/apps/chronon3d_cli/commands/video/exporters/pipe_timing_sidecar_summary.cpp` — blocco `job`.
- `Chronon3d/apps/chronon3d_cli/commands/video/exporters/video_export_native_support.cpp:496` — `timeline_eval_ms` dal contatore.
- `Chronon3d/include/chronon3d/core/profiling/render_counter_macros.hpp` — contatori.
- `Chronon3d/include/chronon3d/timeline/compiled_composition.hpp` — `SceneExecutionMode`.
- `Chronon3d/include/chronon3d/render_graph/pipeline/frame_parameter_table.hpp` — tabella per-frame.
- `Chronon3d/include/chronon3d/render_graph/compiler/compiled_frame_graph.hpp:295` — `prepared_parameters`.
- `Chronon3d/src/timeline/compile_evaluate.cpp:91-93` — `DynamicCallback`/`is_static_topology=false`.
- `Chronon3d/tests/cli/test_timing_sidecar_schema.cpp` — schema del sidecar.

### Documenti

- [`docs/stato/refactored.md`](../../../docs/stato/refactored.md) §1 — gate 100/min, run r49/r101/r102.
- [`docs/stato/chronon3d.md`](../../../docs/stato/chronon3d.md) §11 — test rossi noti, lane stills.
- [`docs/stato/pipeline-ops.md`](../../../docs/stato/pipeline-ops.md) §4 — run Garlasco.
- [`TICKET-PERF-FRAME-PROOF-CHEAP`](TICKET-PERF-FRAME-PROOF-CHEAP.md) — gate duplicati.
- [`TICKET-PERF-TIMELINE-FLATBUFFER`](TICKET-PERF-TIMELINE-FLATBUFFER.md) — gate `timeline_eval`.
- [`TICKET-100X-PERF-WAVES`](TICKET-100X-PERF-WAVES.md) — B2/preset NVENC.
- [`TICKET-MAX-THROUGHPUT-OVERLAY-ARCHITECTURE`](../../../refactored/docs/operations/TICKET-MAX-THROUGHPUT-OVERLAY-ARCHITECTURE.md) — 100/min, VRAM, packed.
- [`100x-ideas.md`](../../../100x-ideas.md) e [`100x-IDEE-NUOVE-2026-10-31.md`](../../../100x-IDEE-NUOVE-2026-10-31.md) — catalogo delle idee; gains `[IPOTESI]`.

---

## 10. NOT RUN / domande aperte

- `[MISURA]` P0: runtime r101-like conferma `compiled_program`, timing nullable e campi `program_sample`/`scene_materialize`; `[NOT RUN]` restano il run r49-like `compiled_composition`, il test schema C++ e il confronto SHA pre/post.
- `[MISURA]` P1: 3 run completati, ma il gate non passa perché il percorso è `compiled_program` e gli overlay SHA differiscono; non usare questi tempi come baseline r49.
- `[NOT RUN]` P2: nessun produttore di `prepared_parameters`; nessun test byte-identico.
- `[NOT RUN]` P3: nessuna tavola flatbuffer; nessun profilo per-frame al 100%.
- `[MISURA]` P4: percorso `compiled_program` attribuito e `program_sample_ms` misurato; `[NOT RUN]` gate di attribuzione ≥90% e confronto SHA pre/post P0.
- `[NOT RUN]` P5: mix 100 item con mappe non eseguito.
- `[DA VERIFICARE]` P6.1: lo stato attuale del wiring della cache overlay.
- `[DA VERIFICARE]` P6.3: stato di certificazione DirectYUV e dei 15 gate zero-copy.
- `[DA MISURARE]` qualità differenziale `p4` vs `slow`; oggi è una decisione owner, non una misura.
- `[DA DECIDERE]` P7: 100× di throughput (cache/stream-copy) o 100× di creazione (EDL/proxy), oppure ritiro dell'obiettivo.

---

## 11. Nota di manutenzione

Dopo la creazione di questo file, l'indice `docs/stato/README.md` va rigenerato:

```bash
python3 ops/tools/reconcile_tickets.py --write
python3 ops/tools/reconcile_tickets.py --check
```

Il ticket non modifica i canonici (`CURRENT_STATUS`, `ROADMAP`, `RELEASE_GATE`, `FOLLOWUP_TICKETS`): è una scheda di piano. Le modifiche di codice previste da P0–P4 richiedono test mirati e, per P2/P3, il gate byte-identico.

---

## 12. Inventario 2026-11-03 — assurdità da eliminare e demolizioni

Scopo: elencare **tutto** ciò che oggi impedisce di arrivare al massimo guadagno disponibile, con prova e gate. La regola di §0 vale anche qui: una voce senza prova è `[DA VERIFICARE]`, non un requisito.

### 12.1 Assurdità attive, ordinate per danno

| ID | Assurdità | Prova | Danno | Cosa fare |
|---|---|---|---|---|
| **A2** | Ogni job è `Serial` **per classe**, non per VRAM: `decide_render_admission` regola 1 restituisce `Serial` per `MonolithicSamePlan` a prescindere dal footprint. `kDefaultRenderJobClass = MonolithicSamePlan` e nessun producer dichiara altro. | `[CODICE]` `daemon_render_concurrency.hpp:159-163` e `:129-130`; commento «No producer declares chunk membership today» | Gli 85 frammenti di r49 sono finestre **dello stesso piano composto** (`finalJobComposePlanSegments`) eseguite una alla volta **per daemon**: in produzione girano 3 daemon = 3 lane. Il danno è «nessuna concorrenza dentro il daemon», non «nessuna concorrenza». | **Verificato 2026-11-03: la correzione proposta è insufficiente e la classe è sbagliata.** Classe giusta = `ChunkedDisjointSamePlan` (finestre disgiunte dello stesso piano), non `DifferentPlan`. Un campo nel producer **non basta**: (a) con il charge di 8 800 MiB su 16 GiB `peak_footprints_that_fit = 1` ⇒ `Serial` anche con la classe corretta; (b) `parallel_disjoint` è forzato a `false` per ogni piano composito (`daemon_service_ipc_render_job.cpp:115-123`); (c) il dominio condiviso per due FullGraph compositi è stato **misurato fallire** (`docs/evidence/gpu-admission-two-jobs-2026-11-02.md` §3: stallo 75 s, morte del daemon). Il lever oggi è per-processo. |
| **A1** | Floor VRAM **hardcoded a 8 800 MiB** e applicato a ogni job FullGraph, prima del compile. | `[CODICE]` `daemon_render_concurrency.hpp:45-46` (`kMeasuredFullGraphPeakBytes = 8800 MiB`), usato in `daemon_service_ipc_render_job.cpp:125-128`; `[MISURA]` shape overlay **592/593 MiB** (2 run) e **~764 MiB/job** a due runtime concorrenti (`docs/evidence/gpu-admission-two-jobs-2026-11-02.md`) | Sovra-addebito reale ma **non certificabile a 14,9×**: 592/593 MiB è la shape overlay, 8 800 MiB è la lane **video FullGraph** (decode+compositing), mai misurata su quella shape. Rapporto indicativo 11,5–14,9×; ciò che resta vero è che il floor da solo esclude ogni overlap anche a classe corretta. | Floor per shape, dal footprint compilato (`PREPARE_PLAN` obbligatorio o sidecar del piano), non da costante globale. La macchina esiste già (`derive_vram_working_set`) ma l'ammissione del daemon usa solo la costante. |
| **A3** | La capacità **pubblicizzata** e quella **ammessa** rispondono a domande diverse: `resolve_render_job_slots` deriva `kRenderJobExecutionSerialized` dall'input *chunked* (1 GiB → 16 footprint ⇒ `false` ⇒ restituisce `requested`, default 2), ma ogni job reale riceve il verdetto `Monolithic` ⇒ `admitted_slots = 1`. | `[CODICE]` `:192-199`, `:205-207`, `:254-255`; `[MISURA]` `refactored/scripts/systemd/pipelinegen.service.d/gpu-slots.conf:7-8` — «`kSerializedRenderJobSlots = 1`, so N daemons = N render lanes» | Il daemon dichiara 2 e ammette 1. La concorrenza reale si compra con **processi**, non con slot. | Risolvere gli slot dalla classe che il daemon serve davvero, oppure **eliminare** la variabile e dichiarare la concorrenza per-processo. Nota 2026-11-03: con la classe chunked il verdetto parallelo restituirebbe `admitted_slots = peak_footprints_that_fit` (16), non `requested` (2): un secondo drift nella stessa funzione. |
| **A5** | `execution_mode` è hardcoded a `DynamicCallback`; `prepared_parameters` è dichiarata e serializzata ma **nessun produttore la riempie**, quindi `is_static_topology` è sempre `false` e la scena si valuta sempre dal callback dinamico. | `[CODICE]` `src/timeline/compile_evaluate.cpp:91,93`; `compiled_frame_graph.hpp:295`; `composition_frame_single.cpp:35-56`; assegnamenti solo nei test (`test_template_program.cpp:96`, `test_frame_delta_compiler.cpp:47`) | Senza identità a monte la decisione «frame duplicato» arriva **dopo** la valutazione: è la radice del costo, e `identity_fallback_frames` non è un discriminante. | P1 di `TICKET-PERF-FRAME-PROOF-CHEAP`: tavola `frame → digest` dal compilatore. **Corretto 2026-11-03:** `graph_reused_frames` **non** dipende da `is_static_topology` — lo decide `graph_result.graph_reused` in `scene_temporal_execution.cpp:502-506`, e su `render_program` il contatore non viene emesso affatto. La catena «static topology ⇒ reuse 0» è un'inferenza non provata. |
| **A6** | `timeline_eval_ms = 0.0` nei run di produzione: il campo che spiega il 92% del loop non viene emesso, e lo `0.0` lo fa leggere come «costo zero». | `[MISURA]` 128/128 campi a `0.0` in `job-full-r4`, `-r101`, `-r102` (verificato 2026-11-03); `[CODICE]` emissione a `video_export_native_support.cpp:496` | Attribuzione impossibile: ogni analisi sul percorso compilato parte da un numero falso. | **Corretto 2026-11-03:** il campo è già `std::optional` (`pipe_timing_sidecar.hpp:233`) e vari rami emettono `nullopt` (`pipe_export_profile.cpp:92`, `video_export_native.cpp:220,234,349`). Il difetto non è il tipo ma `:496`, che emette il contatore anche quando non è mai stato incrementato. Fix: `null` se `timeline_eval_wall_us == 0` e il ramo non è tracciato. |
| **A11** | Upload asset a **piena risoluzione in `Rgba32Float`** (16 B/px) **prima** che il placement conosca il `dst_rect`. | `[CODICE]` `src/render_graph/nodes/source_node_native_image.cpp:154-171`, `:278-286`; `src/backends/assets/image_cache.cpp:255` (`width*height*4` float, key indipendente dal `dst_rect`) | ~300 MB/item ⇒ **8 upload = ~2,4 GB, non 8,8 GB** (corretto 2026-11-03; l'audit citava «242 MB di image bytes»). Resta vero che questi upload dominano il working set e crescono con gli asset. | N4: contratto fit-to-canvas, chiave cache `(digest, canvas-fit)`. |
| **A12** | Cache overlay **installata ma non misurata**: wiring solo se `CacheDB != nil` **e** kill switch on; il dedup di coda è aggirato da `freshRender` con `time.Now()`. | `[CODICE]` `refactored/internal/app/wiring/script_generation_render_cache.go:24`; `render_queue_submit.go:218-224`; `[MISURA]` `closeout-20261030-r3`: `cache_timing.lookup_us.count = 1`, `outcomes = {miss_stored: 1, not_applicable: 17}` | **Corretto 2026-11-03: «spenta» è falso.** La cache è interrogata (1 lookup per composizione, livello manifest) e scrive (`miss_stored:1`); `not_applicable` sui batch è per disegno (`TICKET-MAX-THROUGHPUT` §11.1) e i batch sono 17, non 16. Il numero **65-76 s non ha artefatto**: sono finestre overlay di altri run (`closeout-verify1` 75,1 s, `gate-100-cross-r1` 71,7 s, `closeout-r1` 71,6 s). Il costo del re-render identico **non è quantificato**. | Quantificare prima di progettare: rerun dello stesso overlay SHA contando `outcomes.hit` vs `miss_stored`. Gate: hit >0, non «accendere il wiring». |
| **A4** | `dirty_area_ratio = 1.0` su **ogni** frame; `dirty_rects` è `false` nei test SDK. Il macchinario tile esiste ma non si arma mai. | `[MISURA]` T03 **126/126** frame a `1.0` (verificato 2026-11-03; il «32/32» precedente era di un altro artefatto); `dirty_rects=false` in `tests/install_consumer/*.cpp` e `tests/sdk/*`; `[CODICE]` `tile_execution_policy.cpp`, `frame_delta_compiler.cpp` | Vale **≤3 ms su 46,3**: stima, non misura. Non è la leva ora, ma va armato dopo P1/N3. | Ultimo passo, non primo (§12.3). |
| **A7** | Telemetria **quadruplicata**: ogni misura compare in `job.result.result` e `result.result`, e due volte per nodo. | `[MISURA]` 32 nodi × 2 blocchi × 2 copie = **128 campi** `timeline_eval_ms` per **9** misure reali (r101) | Payload gonfio e rischio concreto di leggere la copia sbagliata (è già successo nell'analisi r101/r102). | Una sola sede autorevole per nodo; il resto derivato. |
| **A8** | `graph_executed_frames = 252` con `frames_total = 126`. | `[MISURA]` `e2e_cert/T03/corrected-text3d-isolated.mp4.timing.json`; `[CODICE]` due `fetch_add` sulla **stessa** frame: `scene_temporal_execution.cpp:503` e `pipe_export_render_loop.cpp:145` | **Chiuso 2026-11-03: doppio conteggio, non doppia esecuzione.** Prova: rapporto esattamente 2,0 in tutti i sidecar con frame>0 (150→300, 210→420, 240→480, 122→244, anche 1→2) e una sola `render_frame()` per frame. Danno: ogni derivata per-frame (`graph_total_ms / graph_executed_frames`, `benchmark_report.hpp:70`) è **dimezzata** (T03: 21,7 ms/frame invece di 43,4). | Un solo incremento: togliere il `fetch_add` del loop CLI e derivare il contatore dal grafo; test che imponga `graph_executed_frames == frames_total` su un job FullGraph. |
| **A9** | `frames_over_budget = 126` su 126: **ogni** frame oltre budget, e nessun gate fallisce. | `[MISURA]` T03 | Il budget esiste, è violato al 100%, e non produce un verdetto: non è un budget. | O il budget è un gate, o è rumore da eliminare. |
| **A10** | `identity_fallback_frames` presente in **7/24** sidecar item e 3/9 riepiloghi batch. | `[MISURA]` r102 | Un fallback non propagato è un fallback non misurato: il gate P2 «zero nei batch certificati» non è verificabile. | Propagazione completa prima di usarlo come gate. |
| **A13** | Packed superbatch: **43,73 item/min** contro 69,45 (controllo) e 101,71 (picco), ancora nel tree come flag. | `[MISURA]` `refactored/ops/jobs/overlay_throughput_golden_100_20261022/results/superpack-20261030-r1/summary.json` (68 item / 93,307 s) e `superpack-control-20261030-r1` (70 item / 60,476 s) — verificati 2026-11-03 | **Percentuale corretta: −37% vs controllo, −57% vs picco**, non «peggiora del 43%»: 43,73 è item/min, non una percentuale. Resta una leva che peggiora e disponibile. | Se non si riprova con una spiegazione, si **elimina** il flag. |
| **A14** | `prepare_join` da **2,000 s** a **12,302 s** tra r101 e r102 **con lo stesso SHA overlay** (`227da1b9…`); wall 46,482 → 66,736 s. | `[MISURA]` r101/r102 — **confermata 2026-11-03**: `duration_ms` 2000 → 12302, quota 4,30% → 18,43%, SHA identico ×34 in entrambi i file | Una fase che varia 6× sullo stesso payload rende non misurabile tutto ciò che sta a valle. La **causa è attribuita al ramo semantico**: due run 20 min hanno `last_branch=semantic`, `assets_ms=264/3` e `scene_analysis` 65,7/39,9 s; il residuo ~21,2/20,2 s non è ancora suddiviso. | Attribuzione fatta 2026-11-03; il gate ±10% **non passa** (`semantic_ms` −30,9%), quindi A14 resta fuori dai report come numero causale finché non c'è un replay same-content. |
| **A15** | Run usato come base con `software_encode_frames = 126`, `video_pipe_fallback_frames = 126`, `vulkan_frames = 0`, `nvenc_frames = 0`. | `[MISURA]` T03 | Il «certified» è interamente software: i suoi tempi non sono il percorso GPU. | Dichiarare la lane nel summary e non mescolare baseline software e GPU. |

### 12.2 Cosa eliminare (non ottimizzare)

1. **`kMeasuredFullGraphPeakBytes` come costante globale.** Va sostituita da un floor per shape. La costante non è sbagliata come *misura*, è sbagliata come *default universale*.
2. **`CHRONON_RENDER_JOB_SLOTS`**, se non può cambiare il verdetto (A3). Una variabile che il commento stesso definisce «un'illusione» è debito, non configurazione.
3. **La regola 1 del policy così com'è.** `MonolithicSamePlan ⇒ Serial` è corretto per un piano intero; è falso per gli 85 frammenti di r49, che sono **finestre disgiunte dello stesso piano** (`finalJobComposePlanSegments`) e oggi ci finiscono per default e non per disegno. La classe esiste (`ChunkedDisjointSamePlan`, non `DifferentPlan`), il default è sbagliato — ma cambiarlo **non basta**: servono il footprint per shape (A1) e il via libera del dominio condiviso, oggi negato dal guard `requires_composition`.
4. **Il default `0.0` di `timeline_eval_ms`** (→ `null`).
5. **La telemetria duplicata ×4** (A7).
6. **Il flag packed superbatch**, se non si riprova (A13).
7. **`copy_gop_source()`** come lane (residuo dichiarato in `TICKET-SCALE-P0`), quando la lane copy passa alle primitive. Resta come oracle finché il parity gate ne ha bisogno: non è un residuo arbitrario.
8. **Il container come leva.** Mux misurato: `mux_packet_ms` 11,968 + `mux_finalize_ms` 0,980 + `encoder_open_mux_header_ms` 0,793 = **13,74 ms su 14 023 = 0,098%** del job (T03). Annex-B, `shm` e DMA-BUF vanno chiusi con questi tre numeri, non riaperti.
9. **P6.6 EDL/player** dal perimetro: è una decisione di prodotto, non una fase di questo piano.
10. **`frames_over_budget`** se non diventa un gate (A9).

### 12.3 Ordine d'attacco e guadagno atteso

L'ordine non è per sforzo: è per **cosa sblocca cosa**. A1/A2 non velocizzano un frame, moltiplicano le lane.

| # | Voce | Effetto | Valore |
|---|---|---|---|
| 1 | **Domanda sul dominio condiviso** (prerequisito): due FullGraph compositi nello stesso daemon | decide se N lane in-process è persino un obiettivo | `[MISURA]` 2026-11-02: stallo a 75 s / morte del daemon ⇒ oggi la risposta è «N processi, non N slot» |
| 2 | **A1+A2 insieme**: `ChunkedDisjointSamePlan` per le finestre disgiunte **e** floor per shape | da 1 lane/daemon a N solo se il footprint entra ≥2× | `[INFERITO]`; **A2 da solo non cambia nulla** (8 800 MiB ⇒ 1 footprint ⇒ `Serial`) |
| 3 | **A5 + P1** identità prima della eval | 46,3 → ~24 ms/frame; duplicati 43,5 → ≤2 ms | `[MISURA]` r49 + `[ARITMETICA]`: 763×48,1 + 844×2 = **38,4 s** da 74,3 s |
| 4 | **N3** blocco parametri baked | eval 42,6 → 0,26-0,74 ms (benchmark saturation) | `[MISURA]` benchmark; `[ARITMETICA]` ~**5,6 s** totali ⇒ ~13× |
| 5 | **A4** dirty tile, a frame sceso sotto ~8 ms | ultimo 30% del GPU draw | `[MISURA]` ≤3 ms su 46,3 oggi |
| 6 | **A11** fit-to-canvas | 300 MB → 20-50 MB per item | `[MISURA]` audit N4; sblocca le lane |
| 7 | **A12** cache overlay | elimina run interi, non frame | `[MISURA]` 65-76 s/run |
| 8 | **N5** hash in-stream | per-item 2,75 → ~1,5 s | `[MISURA]` audit N5 |

### 12.4 Tetto risultante su questa macchina

| Stadio | ms/frame | × realtime | item/min (73 frame/item) |
|---|---:|---:|---:|
| Oggi (r49) | 46,3 | 0,88× | 21 |
| + P1 | ~24 | 1,7× | 42 |
| + N3 | ~3,5 media | ~12× | ~287 |
| + dirty tile | **~2,3 media** | ~20× | ~357 |
| Tetto encoder `slow` (244,97 fps) | — | — | **201** |
| Tetto encoder `p4` (408,4/422,1) | — | — | **341** |
| Tetto encoder `p2` (714,6) | — | — | **587** |

Conseguenze `[ARITMETICA]`:

- **200 overlay/min richiede 243 fps** (3,33 item/s × 73 frame). A `slow` l'engine fa 244,97: **200/min è esattamente la capacità fisica a preset default**. A `p4` c'è 1,7× di margine.
- **Tetto pratico: ~350-450/min**, limitato da VRAM (7 616 MB di picco già oggi su 16 376), per-item overhead e **un solo motore NVENC** (2 sessioni misurate a 1,65-1,9×, non 2×).
- **100× = 7 732/min ⇒ 9 407 fps ⇒ 13× l'engine a `p2`.** `[MISURA]` + `[ARITMETICA]`. Non è raggiungibile ottimizzando: richiede che quei frame non vengano prodotti (quota servita da cache/stream-copy).

### 12.5 Gate dell'inventario

1. Due job overlay indipendenti si sovrappongono (`ParallelDisjoint`), con `vma_usage_bytes` misurato e wall inferiore alla somma seriale.
2. Il floor VRAM del job è quello misurato per lo shape, non 8 800 MiB, e il verdetto non cambia se la VRAM libera scende sotto il floor.
3. `admitted_slots` del verdetto reale coincide con la capacità pubblicizzata, oppure la variabile d'ambiente è stata eliminata.
4. `timeline_eval_ms` è `null` o un numero reale, mai `0.0` per assenza di emissione.
5. `graph_executed_frames == frames_total` oppure la differenza è spiegata nel sidecar.
6. `identity_fallback_frames` è presente in **tutti** i sidecar e vale 0 nei batch certificati.
7. `dirty_area_ratio < 1.0` su almeno un frame di un job reale, con parità pixel.
8. A8 è chiusa il 2026-11-03 (doppio conteggio, `[MISURA]`): `graph_executed_frames` deve tornare `== frames_total` prima di usare qualunque derivata per-frame. A14 è `[CAUSA ATTRIBUITA AL RAMO SEMANTICO]`: i numeri sono verificati; il gate ±10% non passa.

Nessun punto si chiude con «sembra più veloce»: serve il before/after sullo stesso payload e sullo stesso SHA.

### 12.6 Verifica indipendente — esito (2026-11-03)

Ogni voce è stata riletta **sul codice e sugli artefatti locali**, non sulle citazioni di questo ticket. Stato: `[CONFERMATO]` fatto e numero riprodotti; `[CORRETTO]` fatto vero, numero o conclusione sbagliati; `[SMENTITO]` l'affermazione non regge.

| ID | Stato | Cosa è stato riprodotto | Cosa cambia |
|---|---|---|---|
| A1 | `[CORRETTO]` | costante a `:45-46`, applicata pre-compile in `daemon_service_ipc_render_job.cpp:125-128`; 592/593 MiB reali | il 14,9× confronta shape diverse (overlay vs video FullGraph). Il difetto del floor universale resta |
| A2 | `[SMENTITO]` nella conclusione | `Serial` per classe, `kDefaultRenderJobClass`, nessun producer: tutti veri | classe corretta `ChunkedDisjointSamePlan`; il campo nel producer non produce lane (footprint 1, guard `requires_composition`, dominio condiviso misurato in stallo) |
| A3 | `[CONFERMATO]` | slots=2 dalla classe chunked, verdetto reale=1 | — (aggiunto: `admitted_slots` userebbe `peak_footprints_that_fit`, secondo drift) |
| A4 | `[CONFERMATO]` | `dirty_area_ratio=1.0` su 126/126 frame T03; `dirty_rects=false` negli SDK | il «32/32» era di un altro artefatto; «≤3 ms» resta stima |
| A5 | `[CORRETTO]` | `DynamicCallback` e `is_static_topology=false` a `:91,93`; nessun produttore di `prepared_parameters` | `graph_reused_frames` non dipende dalla static topology e su `render_program` non è emesso |
| A6 | `[CORRETTO]` | 128/128 campi a `0.0` in r4/r101/r102 | il campo è già `optional`: il difetto è l'emissione a `:496`, non il tipo |
| A7 | `[CONFERMATO]` | 32 nodi × 2 blocchi × 2 copie; 9 batch | — |
| A8 | `[CONFERMATO]` + risoluzione | 252 vs 126; **due** `fetch_add` sulla stessa frame | doppio conteggio: le derivate per-frame sono dimezzate (T03 21,7 vs 43,4 ms) |
| A9 | `[CONFERMATO]` | 126/126 oltre budget, `frame_budget_ms` 41,67 | — |
| A10 | `[CONFERMATO]` | 7/24 item, 3/9 batch (r102) | — |
| A11 | `[CORRETTO]` | float RGBA a piena risoluzione, cache key indipendente dal `dst_rect` | 8 upload = ~2,4 GB, **non 8,8 GB** |
| A12 | `[CORRETTO]` | wiring:24, `freshRender`:218-224; `lookup.count=1`, `miss_stored:1`, `not_applicable:17` | la cache **è** interrogata: «spenta» è falso; i 65-76 s sono finestre overlay di altri run, non re-render misurato |
| A13 | `[CORRETTO]` | 43,73 / 69,45 / 101,71 item/min su summary.json | −37% vs controllo e −57% vs picco, non «−43%» |
| A14 | `[CONFERMATO]` | 2000 → 12302 ms, quota 4,30 → 18,43%, SHA identico ×34 | numeri solidi; **causa attribuita al ramo semantico**, ripetibilità ±10% non dimostrata |
| A15 | `[CONFERMATO]` | 126 software, 126 fallback, 0 vulkan, 0 nvenc | — |

Non resta nulla di «da verificare»: A8 è chiusa, A12 e A13 sono ricondotte ai loro artefatti. Resta **non quantificato** (non non-verificato) il costo del re-render identico (A12); la causale di A14 è attribuita, ma la ripetibilità ±10% non è dimostrata.

### 12.7 To-do per voce (chiusura verificabile)

**A1 — floor per shape** (owner: questo ticket; codice `daemon_render_concurrency.hpp`)

- [ ] misurare il picco della shape **video FullGraph** con `cmd/vram-probe`, 2 run, report allegato (oggi «nessun numero lo copre»)
- [ ] passare il footprint compilato all'ammissione del daemon (`derive_vram_working_set` esiste, il daemon usa solo la costante)
- [ ] mantenere `max(derived, floor)` e pubblicare `calibration_gap_bytes` nel sidecar
- [ ] gate: sidecar con `peak_vram_bytes` per job; verdetto invariato al calare della VRAM libera

**A2 — classe e dominio condiviso** (owner: questo ticket + `TICKET-FINAL-JOB-SPEEDUP-LEVERS` L5)

- [ ] correggere la classe attesa in `ChunkedDisjointSamePlan` (le 85 finestre sono dello stesso piano)
- [ ] **non** rimuovere il guard `requires_composition` prima di un test che due FullGraph compositi convivono nello stesso daemon
- [ ] decidere e documentare: N daemon (misurato 1,98×) al posto di N slot in-process
- [ ] gate: 2 job indipendenti sovrapposti, `vma_usage_bytes` misurato, wall < somma seriale

**A3 — slot pubblicizzati = slot ammessi** (owner: questo ticket; codice `daemon_render_concurrency.hpp`)

- [ ] allineare `admitted_slots` a `min(requested, peak_footprints_that_fit)`
- [ ] derivare `kRenderJobExecutionSerialized` dalla classe che il daemon serve davvero, non dalla più favorevole
- [ ] oppure eliminare `CHRONON_RENDER_JOB_SLOTS` e dichiarare 1 lane per daemon
- [ ] test: slot pubblicizzati == `admitted_slots` del verdetto reale

**A4 — dirty tile** (owner: questo ticket; ultimo per ordine)

- [ ] armare `dirty_rects` su un job reale e misurare il delta con parità pixel
- [ ] gate: `dirty_area_ratio < 1.0` su almeno un frame

**A5 — identità a monte** (owner: `TICKET-PERF-FRAME-PROOF-CHEAP` P1)

- [ ] collegare `FrameParameterTable` al compilatore di produzione (unico produttore di `prepared_parameters`)
- [ ] non usare `graph_reused_frames` come prova del ramo statico finché `render_program` non lo emette
- [ ] gate: `identity_fallback_frames` presente in tutti i sidecar e 0 nei batch certificati

**A6 — telemetria timeline** (owner: questo ticket §8)

- [ ] emettere `null` quando `timeline_eval_wall_us == 0` e il ramo non traccia (`video_export_native_support.cpp:496`)
- [ ] aggiungere `program_sample_ms` per il ramo `render_program`
- [ ] gate: nessun `0.0` da assenza di emissione in 3 run

**A7 — telemetria ×4** (owner: questo ticket)

- [ ] una sola sede autorevole per nodo; `summary.first_frame.render` derivato da `cpu_breakdown`
- [ ] rimuovere la doppia copia `result.result` o documentarla come alias
- [ ] gate: 1 campo per nodo (32), non 128

**A8 — contatore doppio** (owner: questo ticket; chiusa la diagnosi)

- [ ] rimuovere uno dei due `fetch_add` (`pipe_export_render_loop.cpp:145` o `scene_temporal_execution.cpp:503`)
- [ ] test di regressione: `graph_executed_frames == frames_total` su un FullGraph da 126 frame
- [ ] ricalcolare le derivate per-frame nei report esistenti (T03: 43,4 ms, non 21,7)

**A9 — budget o rumore** (owner: questo ticket)

- [ ] trasformare `frames_over_budget` in gate oppure toglierlo dal sidecar
- [ ] se gate: budget per lane (41,67 ms è il frame-budget a 24 fps, non un obiettivo engine)

**A10 — propagazione fallback** (owner: `TICKET-PERF-FRAME-PROOF-CHEAP`)

- [ ] propagare `identity_fallback_frames` a item, batch e top-level
- [ ] gate: presente in tutti i sidecar, 0 nei certificati

**A11 — fit-to-canvas** (owner: questo ticket + `TICKET-SCALE-P0-ASSET-VRAM-GOP` N4)

- [ ] contratto fit-to-canvas e chiave cache `(digest, canvas-fit)`
- [ ] misurare il working set con 8 asset prima/dopo (atteso ~2,4 GB → 160–400 MB)
- [ ] gate: sidecar con `gpu_asset_bytes` per job

**A12 — cache overlay** (owner: `TICKET-MAX-THROUGHPUT-OVERLAY-ARCHITECTURE` §11)

- [ ] rerun dello stesso overlay SHA contando `hit` vs `miss_stored` (oggi 1 lookup, 0 hit misurati)
- [ ] decidere se il dedup di coda (`freshRender`) deve restare; se resta, documentare che la cache è content-addressed e il job ID serve solo all'idempotenza
- [ ] gate: `outcomes.hit > 0` con SHA identico; nessun numero in secondi finché non è misurato

**A13 — packed superbatch** (owner: `TICKET-MAX-THROUGHPUT-OVERLAY-ARCHITECTURE` §11.6)

- [ ] correggere «−43%» in −37%/−57% nei documenti che lo citano
- [ ] decidere: eliminare il flag o riprovare spiegando `mean_frame_ms` 55-63 ms
- [ ] gate: una run packed con item/min ≥ controllo, altrimenti remove

**A14 — prepare_join** (owner: `TICKET-FINAL-JOB-SPEEDUP-LEVERS` L2)

- [x] attribuire la fase con telemetria dedicata, non per differenza —
  `[MISURA]` 2026-11-03: due run Garlasco 20 min
  (`job_1793737130845286137_5bd2965f`, `job_1793737420046345110_e5cb94e8`)
  hanno `last_branch=semantic`, `assets_ms=264/3`, `semantic_ms=86.943/60.049`,
  `wall_ms=86.789/59.907`. La causa è il ramo semantico, non gli asset;
  `scene_analysis` vale 65.745/39.862 ms.
- [ ] gate: due run consecutive con la stessa fase entro ±10% — **NON PASSA**:
  `semantic_ms` 86.943 → 60.049 (−30,9%) e `scene_analysis` 65.745 → 39.862
  (−39,4%). Il contenuto è stato rigenerato; serve un replay same-content o
  una cache del `scene_analysis` prima di dichiarare A14 chiuso.

**A15 — lane dichiarata** (owner: questo ticket §8)

- [ ] dichiarare la lane nel summary (`software`/`vulkan`/`nvenc`) e non usare T03 come baseline GPU
- [ ] gate: nessun confronto tra run di lane diverse

### 12.8 Ticket toccati da questa verifica

- `Chronon3d/docs/tickets/TICKET-PERF-100X-VERIFIED-PLAN.md` (questo): §12.1 corretta, §12.6 esito, §12.7 to-do.
- `Chronon3d/docs/tickets/archive/TICKET-PERFORMANCE-AUDIT-2026-10.md`: aritmetica A11 (2,4 GB, non 8,8 GB).
- `Chronon3d/docs/tickets/TICKET-SCALE-P0-ASSET-VRAM-GOP.md`: perimetro dell'8,8 GB (lane video, non overlay).
- `refactored/docs/operations/TICKET-MAX-THROUGHPUT-OVERLAY-ARCHITECTURE.md`: `cache_outcome` e percentuali packed.
- `refactored/docs/operations/TICKET-FINAL-JOB-SPEEDUP-LEVERS-2026-11-02.md`: classe di ammissione dei frammenti L5.

---

## 13. Intervento 2026-11-03 — cache topologica condivisa (P4)

**Fatto `[CODICE]`.** `SoftwareRenderer::render_program` costruiva un
`runtime::RenderPipeline` temporaneo per ogni chiamata; la cache topologica
(32 entry) era quindi per-pipeline e moriva con la chiamata. Nel job P4 le
72 materializzazioni con 0 hit sono compatibili con tre passaggi per item
(probe/warmup/loop) che ripartivano da una cache fredda.

La cache ora vive in `SoftwareExecutionContext::topology_cache`
(`std::shared_ptr<runtime::SceneTopologyCache>`), inizializzata dal
`SoftwareRenderer` e condivisa da tutte le pipeline dello stesso renderer;
resta il fallback per-pipeline quando il contesto non la porta. Il contatore
`scene_materialize_count` continua a contare solo i miss; gli hit passano da
`scene_materialize_cache_hit_count`.

**Verifica locale `[MISURA]` (2026-11-03, render software 2 frame, piano
`output/T16_chronon_render/scene.plan.json`, `--warmup-dummy-frame`):**
`evaluation_mode=compiled_program`, `timeline_eval_ms=null`,
`scene_materialize_count=1`, `scene_materialize_cache_hits=1`,
`graph_executed_frames=2`. Prima della cache condivisa ogni chiamata
`SoftwareRenderer::render_program` partiva da una pipeline nuova (miss).

**Gate P4 — replay 2026-11-03 (job `job_1793735303280198261_6e226690`,
payload `r101-like-p4` SHA `75e550c5…`, `SUCCEEDED`):** il conteggio scende
da 72 a **9** batch unici (1 miss per batch) e gli hit passano da 0 a
**1.720**; la somma `scene_materialize_ms` dei 9 batch è **2.007,764 ms**
contro **15.418,692 ms** del P4 baseline (-87,0%). La cache condivisa è quindi
la leva giusta e i fingerprint non sono tutti distinti: il profilo per-item di
`SceneMaterializer` non è il passo successivo.

**Ma il gate SHA non passa.** Il replay ha rigenerato contenuto diverso:
`overlay_plan.fingerprint` `4742d7fe…` vs `49e7cb8a…` baseline; `scenes`
canonico `04090041…` vs `6c24d27d…` (quest'ultimo coincide con il
`scenes_sha256` del manifest); 8 overlay con SHA diverso e 16 artifact ID
nuovi su 24. Il calo è una misura direzionale, non un A/B sullo stesso
contenuto; `overlay_sha256` non è invariato. **P4 resta PARTIAL.**
Prossimo passo: replay render-only con `scenes`/`overlay_plan` bloccati al
baseline, oppure un job che non rigeneri il piano.

**Garlasco 20 min — replay r20minuti 2026-11-03 (job
`job_1793736188766904832_9fb6a467`):** stesso payload del baseline
(`payload_equal=true`), 5 batch, **5 miss / 1.713 hit / 1.124,487 ms** contro
**1.496 / 0 / 404.623,258 ms**; `overlay_render` 46,2 s vs 173,9 s; wall
322,3 s vs 425,2 s (**−24,2%**). Il contenuto è stato rigenerato (`scenes`
canonico `4e6ec2a1…` vs `def8839d…`, piano `e5496fe8…` vs `e90a11f8…`, 0
overlay SHA uguali), quindi il −24,2% end-to-end è direzionale; la cache
resta la leva.

**A14 — `prepare_join` decomposto `[MISURA]` (2026-11-03).** Due run
Garlasco 20 min sullo stesso payload (`r20minuti-cache-20261103.generate.json`,
SHA `61b73145…`) hanno completato con `last_branch=semantic` in entrambi:
`job_1793737130845286137_5bd2965f` → `prepare_join.wall_ms=86.789`,
`semantic_ms=86.943`, `assets_ms=264`, `semantic_wait_ms=86.788`;
`job_1793737420046345110_e5cb94e8` → `59.907`, `60.049`, `3`, `59.907`.
`scene_analysis` è 65.745/39.862 ms; il wall del job è 240,7/147,5 s. Quindi
il join aspetta il **ramo semantico** (VidRush join + preparazione + NLP
localizzato), non il prefetch audio. Il gate di ripetibilità ±10% **non
passa** (`semantic_ms` −30,9%, `scene_analysis` −39,4%) e il contenuto è
stato rigenerato: A14 resta **CAUSA ATTRIBUITA / RIPETIBILITÀ NON
DIMOSTRATA**. La parte residua del ramo semantico (~21,2/20,2 s) comprende
`computeLocalizedAnnotations` e il geocode/enqueue, non ancora suddivisi.
Prossimo passo: replay same-content o cache/parallelizzazione di
`scene_analysis`; non ridurre il budget asset, che vale 264/3 ms.

Evidenza: `docs/evidence/scene-topology-cache-20261103/` (render locale) e
`ops/jobs/garlasco_editorial_e2e_20261030/p4-cache-20261103-*`.
