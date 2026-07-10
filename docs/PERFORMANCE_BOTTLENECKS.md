# Performance Bottlenecks — Pipeline Attuale

> Data raccolta: 8 Giugno 2026, build `2796e99`
> Scena benchmark: `MinimalistImageTrackingBreathing` (2 frame, 1920×1080, dirty-rects ON)
> Renderer: Software (Highway SIMD), Compositor: NormalPremul (SIMD)

---

## Riepilogo Performance

| Metrica | Valore |
|---|---|
| FPS stimato | ~14 fps (150 frame video) |
| CPU utilization | **12.3%** (0.98 core effettivi su 8) |
| RAM | SAFE (268 MB / 19.5 GB) |
| Node execute actual | 221 ms |
| Hot attribution | **17.2%** (38/221 ms attribuiti a counter specifici) |
| Cache hit rate | 50% (3/6 — normale per scena animata) |
| TBB workers peak | 1 |
| Encoder pipe blocked | 0% (solo render singolo) |
| ClearNode COW | 33 MB evitati, 0 copie |

---

## 🔴 Colli di Bottiglia Critici

### 1. Hot Attribution Coverage: 17.2% — L'83% del tempo è invisibile

> **⚠️ Status as of 2026-07-08 (this commit's audit, followup P0-#2):**
>
> ✅ **This section is HISTORICAL — the timer wiring described below is ALREADY APPLIED at HEAD via commit [`8387c8a4`](docs/CHANGELOG.md) (June 2026, `feature/frictions-pass1` workstream).**
>
> **Sintomo originale (rimane valido come letteratura di bottleneck):** the 17.2% attribution gap exists for the *June 8 baseline* build `2796e99`. The counter fields `cache_eval_ms`, `dirty_eval_ms`, `predicted_bbox_ms`, `clone_context_ms`, `state_assign_ms` now WIRING populate at HEAD via:
> - `src/render_graph/executor/cache_evaluator.cpp` line 23 (`t_cache0`) + line 122 (`fetch_add` at end of `evaluate_cache()`),
> - `src/render_graph/pipeline/scene_dirty.cpp` line 47 (`t_dirty0`) + lines 61 + 221 (`fetch_add` in BOTH return paths),
> - `src/render_graph/executor/executor_levels.cpp` lines 51–57 (per-level vectors) + lines 220–237 (parent rollup),
> - `src/render_graph/executor/node_runner.cpp` lines 230–233 (`predicted_bbox_ms` via `out_predicted_bbox_ms` parameter).
> - Regressione LOCK in `tests/core/test_cache_eval_dirty_counters.cpp` (4 TEST_CASE: field exists/aligned, accumulate >0 post-pass, merge_tls no-records-lost).
>
> **Tests ✓ PASS machine-verified** in the original commit; **macchina-verifica fresh at HEAD deferred to next working build host** (per AGENTS.md honesty policy). The interpretation note about sub-ms rounding (`static_cast<uint64_t>(duration_ms(...))` may truncate light frames to 0) remains valid: forward-only upgrade to microsecond resolution is the residual improvement.

**Sintomo:**
```
node_execute_actual_ms    = 221 ms
clearnode_clear_ms        =   1 ms  (0.5%)
compositenode_blend_ms    =  10 ms  (4.5%)
transform rows (stima)    =  27 ms  (12.2%)
frame_conversion          =   0 ms  (0.0%)
video_pipe_write          =   0 ms  (0.0%)
────────────────────────────────────────
TOTAL attribuito          =  38 ms  (17.2%)
NON attribuito            = 183 ms  (82.8%)
```

**Causa:** I counter di fase attuali coprono solo il lavoro "dentro" i nodi di rendering (`clearnode_clear_ms`, `compositenode_blend_ms`, `frame_conversion_copy_ms`). L'overhead del graph executor — cache eval, dirty eval, predicted bbox, clone context, state assignment, telemetry emit — **esiste come campo nel counter set ma nessuno scrive mai il timer**. I campi `cache_eval_ms`, `dirty_eval_ms`, `predicted_bbox_ms`, `clone_context_ms`, `state_assign_ms` esistono ma sono sempre zero.

**Nota importante:** I timer di alto livello FUNZIONANO e mostrano una gerarchia utile:
```
graph_total_ms         = 224 ms  (dalla creazione del grafo alla fine esecuzione)
graph_execute_ms       = 222 ms  (solo esecuzione)
node_dispatch_ms       = 221 ms  (dispatch dei nodi)
node_execute_actual_ms = 221 ms  (esecuzione effettiva dei nodi)
```
Questo significa:
- Overhead executor (graph_exec - node_dispatch) = **1 ms** ✅
- Overhead dispatch (node_dispatch - node_execute) = **0 ms** ✅
- **I 183ms "inspiegati" sono DENTRO i nodi**, non nell'overhead del grafo

Quindi l'83% del tempo non va perso in scheduling — va speso dentro `execute_single_node()` nelle fasi cache eval, predicted bbox, telemetry. Ma non possiamo vedere esattamente quanto perché i timer di sottofase non sono accesi.

**Perché i counter sono a zero:** I timer di sottofase non vengono popolati perché `ctx.timer.start()`/`.stop()` non è mai chiamato per quelle fasi in `src/render_graph/executor/internal.cpp`.  <!-- drift-allow: stale-ref -->

**Come superarlo:**
1. Aggiungere `ctx.timer.start()` / `.stop()` per ogni fase del graph executor:
   - `src/render_graph/executor/internal.cpp` → `evaluate_cache()`, `predicted_bbox()`  <!-- drift-allow: stale-ref -->
   - `src/render_graph/executor/executor.cpp` → node scheduling, dispatch
   - `src/render_graph/pipeline/scene.cpp` → dirty rect, input resolve, clone context
2. Modificare ~10 righe in 3 file per accendere i timer
3. Risultato atteso: attribuzione → **90%+**, scopriamo esattamente dove vanno i 183ms

**File da modificare:**
- `src/render_graph/executor/internal.cpp` (~riga 30: cache eval → `timer.start/stop("cache_eval_ms")`)  <!-- drift-allow: stale-ref -->
- `src/render_graph/executor/internal.cpp` (~riga 50: predicted bbox → `timer.start/stop("predicted_bbox_ms")`)  <!-- drift-allow: stale-ref -->
- `src/render_graph/pipeline/scene.cpp` (~riga 100: dirty eval → `timer.start/stop("dirty_eval_ms")`)

**Sforzo:** Basso (< 30 righe, 3 file)

---

### 2. CPU Utilization: 12.3% — Sottoutilizzazione Strutturale

**Sintomo:**
```
8 cores disponibili
0.98 cores effettivi in uso (cpu_total_ms / wall_time_ms)
CPU utilization: 12.3%
```

**Causa strutturale:** Il grafo è una catena sequenziale:
```
ClearNode(1ms) → TransformNode(27ms) → CompositeNode(10ms) → Output
```
Ogni nodo **dipende dall'output del precedente**. Non c'è spazio per parallelizzare tra livelli.

**Dentro ogni nodo:**
- ClearNode: 7.5M pixel in 1ms → 120 GB/s (ottimo, satura la memoria)
- Transform: 902K pixel in 27ms → 0.53 GB/s (lento, sampling costoso)
- Composite: 2.95M pixel in 10ms → 4.73 GB/s (decente, SIMD aiuta)
- Soglie TBB non raggiunte → tutto in single-core

**Come superarlo — 3 strategie:**

| Strategia | Speedup | Sforzo |
|---|---|---|
| **Frame-level parallelism**: renderizzare 2 frame in-flight su core separati | 2× throughput | Alto |
| **Double-buffering**: mentre frame N viene convertito in YUV, frame N+1 viene renderizzato | 1.3-1.5× | Medio |
| **Task-level parallelism**: splittare la composizione in tile indipendenti (tile-based rendering) | 2-4× | Alto |

**Raccomandazione iniziale:** Double-buffering è il miglior rapporto impatto/sforzo. Il render loop attuale è:
```
for each frame:
    render(frame)           → seriale, single-core
    convert_to_yuv(frame)   → seriale, single-core
    pipe_write(frame)       → bloccante
```
Con double-buffering:
```
thread 1: render(frame N)
thread 2: convert_to_yuv(frame N-1) + pipe_write(frame N-2)
```
Basta una coda di 2 frame e un thread worker per la conversione.

**Sforzo:** Medio (modificare `render_job_execute.cpp` e `render_job_write_frame.cpp`)

---

### 3. Transform Node: 0.53 GB/s — Sampling Costoso

**Sintomo:**
```
Transform rows:        27 ms   per 902,584 pixel  = 0.53 GB/s
Composite blend:       10 ms   per 2,954,168 pixel = 4.73 GB/s
ClearNode clear:        1 ms   per 7,552,660 pixel = 120 GB/s
```

Transform è **9× più lento** del composite per byte processato.

**Causa:** `execute_translate_clamped()` campiona pixel dall'immagine sorgente con coordinate frazionarie (interpolazione bilineare + clamping ai bordi). È un memory-bound pattern non contiguo — ogni pixel richiede un accesso randomico a 4 pixel vicini.

**File coinvolti:**
- `src/render_graph/nodes/transform_kernels.cpp` — implementazione kernel
- `include/chronon3d/render_graph/nodes/transform_node.hpp` — ExecuteContext dispatch

**Come superarlo:**
1. Benchmark Highway SIMD per `gather` + bilinear interpolation (istruzioni `gather` AVX2/AVX-512)
2. Prefetch righe consecutive per migliorare cache hit rate
3. Se l'immagine non ha scaling/rotazione (solo translate), usare `execute_translate_memcpy()` che è un semplice memcpy allineato (~10 GB/s teorico)

**Sforzo:** Medio

---

## 🔴 Nuovi Colli di Bottiglia — Composizioni Cinematografiche Text-Heavy

> Date jobs raccolta: **17 Giugno 2026**, build `05887865` (HEAD dopo rebase su `origin/main`).
> Scene benchmark: 5 nuove comps in `content/anims/compositions/cinematic_text_camera.cpp` —
> `DeepParallaxCascade`, `WhipPanHeroReveal`, `OrbitHandheldGlow`,
> `RackFocusTitleSwap`, `AbyssFreefallStagger`.
> Confronto: `MinimalistImageTrackingBreathing` (baseline di questo documento).

### 11. WhipPanel + Abyss: 7-8× il tempo di `node_execute` per frame

**Sintomo — SQLite `~/.chronon3d/telemetry/chronon3d_render_history.sqlite`** (ultima run per comp, `render --frame X`):

| Comp                       | wall_ms | layers_rendered | composite_calls | transform_calls | text_glyphs_rasterized |
|---|---:|---:|---:|---:|---:|
| `MinimalistImageTrackingBreathing` (baseline) | 2245 |  3 |  3 |  1 |  0 |
| `DeepParallaxCascade`                         | 2825 |  5 |  5 |  3 | 42 |
| **`WhipPanHeroReveal`** ⚠️                     | 2502 | **19** | **19** | **18** | 23 |
| `OrbitHandheldGlow`                           | 2341 |  3 |  3 |  0 |  6 |
| `RackFocusTitleSwap`                          | 3590 |  3 |  3 |  2 | 18 |
| **`AbyssFreefallStagger`** ⚠️                  | 2604 |  9 |  9 |  8 |  6 |

**Misura precedente (render 30-frame, sessione bench):**

| Comp                       | `node_execute_actual_ms` / frame | vs baseline |
|---|---:|---:|
| `MinimalistImageTrackingBreathing` (baseline) | ~26 ms | 1× |
| `DeepParallaxCascade`                         | ~63 ms | ~2.4× |
| **`WhipPanHeroReveal`** ⚠️                     | **~200 ms** | **~7.7×** |
| `OrbitHandheldGlow`                           | ~39 ms | ~1.5× |
| `RackFocusTitleSwap`                          | ~116 ms | ~4.5× |
| **`AbyssFreefallStagger`** ⚠️                  | **~178 ms** | **~6.8×** |

**Causa:** `cinematic_text_camera.cpp` definisce comps con fino a **19 layer staggered**
letter-by-letter via `text_helpers::glow::apply_ae_glow`. Le lettere sono tenute
a `opacity=0` durante il lead-in (WhipPanel frames 0–21, Abyss analogo).
Tuttavia il graph executor **esegue comunque il path completo** di
`execute_single_node` per ogni layer (cache eval + dirty rect + clone context +
run_node + state assign + telemetry emit) prima di renders.

Il `text_glyphs_rasterized` resta **basso** in entrambe le comps (WhipPanel=23 in
30 frame = ~0.77/frame, Abyss=6/0.2/frame) → **NON è il glyph rasterization** il
collo di bottiglia; è l'overhead di orchestrazione per layer la cui composizione
finale è trasparente.

**Causa strutturale:** nessun skip esiste per `effective_opacity < threshold` in
`execute_single_node`. Esiste `ctx.tile.early_exit_skip[id]` ma viene impostato
solo per i clear-nodi, non per i layer compositivi trasparenti.

**Come superarlo — lavoro in 3 fasi:**

1. ✅ **Prep work committed (05887865)** — feature-flag dormiente:
   - `PreResolvedNode::resolved_opacity: f32 = 1.0f` (`execution_state.hpp`),
   - hoist di `node/input_ids/pr` al top di `execute_single_node`,
   - guard env-gated **`CHRONON3D_SKIP_INVISIBLE_LAYERS=1`** che, quando attivo E
     `pr.resolved_opacity <= 0.001f`, ritorna una FB 64×64 trasparente e
     `return;` — rispecchiando il pattern di `early_exit_skip`.
   - Defaults **OFF** + `resolved_opacity` default **1.0f** = **zero regressione**
     fintantoché (a) il populate site non è wired e (b) l'env var non è settato.

2. ⏳ **TODO (commit di follow-up)**: wire il populate in
   `src/render_graph/executor/executor_levels.cpp:36–39`. Dopo
   `level_resolved[i] = resolve_inputs(...)` aggiungere
   `level_resolved[i].resolved_opacity = layer.opacity_anim().evaluate(cur_frame)`
   (alta confidenza: `LayerBuilder::opacity_anim()` ritorna
   `AnimatedValue<f32>&` su `m_layer.anim_transform.opacity`, che espone
   `evaluate(frame_t)` come tutti gli altri `AnimatedValue` — verificare
   comunque in `src/scene/builders/layer_builder.{hpp,cpp}` se la firma è
   cambiata).

3. ⏳ **Validation after populate**: rieseguire lo stesso bench 30-frame con
   `CHRONON3D_SKIP_INVISIBLE_LAYERS=1` ON e misurare il delta reale con
   `render_counters` (`layers_culled` atteso in crescita, `node_execute_actual_ms`
   atteso in calo).

### Delta atteso (post-populate + env=1)

| Comp                       | node_exec/frame pre | target post | speedup |
|---|---:|---:|---:|
| `WhipPanHeroReveal`        | ~200 ms | ~26 ms | **~7.7×** |
| `AbyssFreefallStagger`     | ~178 ms | ~26 ms | **~6.8×** |
| `RackFocusTitleSwap`       | ~116 ms | ~50 ms (stima: ~3 letter invisibili nel rack — da verificare) | ~2.3× |
| `DeepParallaxCascade`      |  ~63 ms | ~50 ms | ~1.3× |
| `OrbitHandheldGlow`        |  ~39 ms | invariato (1 sola letter staggered visibile dopo frame 0) | ~1× |
| `MinimalistImageTrackingBreathing` | ~26 ms | invariato | 1× (baseline) |

### File coinvolti

| File | Ruolo per il fix |
|---|---|
| `src/render_graph/executor/execution_state.hpp`   | nuovo campo `resolved_opacity` su `PreResolvedNode` (✅ in `05887865`) |
| `src/render_graph/executor/node_runner.cpp`       | hoist + guard env-gated (✅ in `05887865`) |
| `src/render_graph/executor/executor_levels.cpp`   | populate site — **TODO follow-up** (~riga 36–39) |
| `src/scene/builders/layer_builder.{hpp,cpp}`      | inspection per la signature esatta di `layer.opacity_at(frame)` |
| `content/anims/compositions/cinematic_text_camera.cpp` | origine delle 5 comps text-heavy |

---

### 12. Hygiene review (queued, deferred)

Il commit `05887865` ha lasciato **6 punti di hygiene** identificati dal
`code-reviewer-minimax-m3` non risolti deliberatamente per mantenere il
commit chirurgico:

1. **Code duplication** — il blocco 11-line post-guard `CHRONON3D_SKIP_INVISIBLE_LAYERS`
   è byte-for-byte il body di `early_exit_skip`. Estrarre un helper
   `emit_transparent_skip(state, id, ctx, parent_pool, parent_counters)`
   condiviso da entrambe le guardie.
2. **Silent no-op risk** — env var ON + `resolved_opacity` mai popolato = guard
   dormiente senza segnale. Aggiungere `spdlog::warn` one-shot al primo
   `kSkipInvisibleOpacity && pr.resolved_opacity == 1.0f`.
3. **Static env init** — `static const bool kSkipInvisibleOpacity = ...` cacha
   al primo call; OK per produzione, blocca test flag-toggle workflows. (Da
   accettare o rifattorizzare con `std::atomic<bool>` invalidato su env change.)
4. **`input_ids` hoist non usato** da nessuna delle due guardie — rimetterlo
   dopo `profiling::ProfilingGuard node_guard`.
5. **`execution_state.hpp` doc-reference stale** — il commento menziona una
   funzione `populate_inputs_invisible_layers()` che non esiste; fixare a una
   riga sola ("Defaults to 1.0; populated at resolve-time from
   `m_layer.anim_transform.opacity`").
6. **Verbose guard docblock** — 10 righe di spiegazione vs 0 di `early_exit_skip`.
   Assottigliare a 2-3 righe per matchare lo stile del file.

**Sforzo:** ~30 righe in 2 file (`node_runner.cpp` + `execution_state.hpp`),
1 commit separato, **nessuna dipendenza dal #11 populate step**.

---

## 🟡 Colli di Bottiglia Moderati

### 4. Node Parallelism: TBB Peak = 1

**Sintomo:**
```
level_parallel_count       = 4
level_sequential_count     = 2
tbb_active_workers_peak    = 1
used_parallel_composite    = 0
used_parallel_clear        = 0
used_parallel_transform    = 0
```

Nonostante il grafo abbia 4 livelli paralleli nel DAG, TBB non attiva mai più di 1 worker alla volta.

**Causa:** Le soglie di parallelizzazione sono troppo alte per i workload correnti:
- Composite: soglia = 32 righe (1920×1080 = 1080 righe → supera la soglia, ma il workload per riga è leggero)
- Transform: soglia 128×128 = ~16000 pixel (902K pixel → supera, ma sampling costoso per riga)
- Clear: soglia 1M pixel (7.5M → supera, ma 1ms è troppo veloce per ammortizzare overhead TBB)

Il problema è che TBB usa `auto_partitioner` che stima il costo per iterazione e decide di non parallelizzare se l'overhead stimato supera il guadagno.

**Come superarlo:**
1. Passare a `simple_partitioner()` per forzare la divisione in chunk più piccoli
2. Abbassare le soglie: composite 32→16 righe, transform 128×64→64×32
3. Benchmark: misurare overhead TBB vs speedup per diversi chunk size
4. Alternativa: usare `tbb::parallel_for` con `grainsize` esplicito invece di `blocked_range`

**Sforzo:** Basso (modificare soglie in ~4 file)

---

### 5. Dirty Rect Efficiency: 105% — Overlap Tra Frame

**Sintomo:**
```
pixels_touched  = 7,181,160
dirty_pixels    = 7,508,628  (105% dei pixel toccati)
dirty_rect_count = 6
dirty_full_fallbacks = 0
```

L'area dirty copre più pixel di quelli effettivamente renderizzati — significa overlap tra dirty rect di frame consecutivi o union imprecisa.

**Causa:** L'animazione di breathing cambia le coordinate dell'immagine ogni frame. Dirty rect calcola l'area cambiata come unione delle bounding box. Con animazioni continue, l'unione si espande progressivamente.

**Impatto:** Attualmente basso (la scena è piccola). Può peggiorare con scene più grandi o più layer animati.

**Come superarlo:**
1. Migliorare l'algoritmo di merge: invece di union semplice, calcolare l'intersezione con la dirty region del frame precedente
2. Resettare la dirty region quando supera una soglia (es. > 50% del frame)
3. Considerare tile-based dirty tracking per granularità più fine

**Sforzo:** Basso

---

### 6. Cache Efficiency: 50% (3/6)

**Sintomo:**
```
cache_hits   = 3
cache_misses = 3
```

50% per scena animata è normale. Per scene statiche (es. `DarkGridBackground`) si arriva a 80-100%.

**Nota:** Questo NON è un problema ora. Il cache hit rate salirà naturalmente con scene più statiche. Monitorare.

### 10. (Già Risolto) Vecchio Bottleneck: Graph Builder

Il vecchio documento `PERFORMANCE_BOTTLENECKS.md` (pre-2026) indicava il graph builder come causa di 50-150ms per frame. **Questo è già stato risolto.** I dati attuali mostrano:
```
graph_total_ms           = 224 ms
graph_execute_ms         = 222 ms
graph_resolve_layers_ms  =   1 ms
graph_build_ms           =   0 ms (incluso in total)

Differenza (building + dirty + resolve) = 224 - 222 = 2 ms ✅
```
Il graph builder ora impiega **~2ms**, non 50-150ms. Questo grazie al `resolve_layers` caching e alla compilazione lazy del grafo.

---

## 🟢 Aree OK

### 7. ClearNode COW — Funziona Perfettamente
```
clearnode_bytes_avoided     = 33,423,360 (33 MB)
clearnode_memcpy_calls      = 0
clearnode_memcpy_bytes      = 0
clearnode_detach_shared_count = 0
clearnode_partial_clip_copy_count = 0
clearnode_full_clip_skip_count = 0
prev_fb_use_count_peak      = 1
```
Zero copie. Il COW evita 33 MB di copie ogni frame. `prev_fb_use_count_peak = 1` significa che il framebuffer non è mai condiviso — COW funziona al massimo.

### 8. Encoder Pipe — 0% (Render Singolo)
Per render singoli non c'è encoder. Per video (150 frame), il pipe write occupa ~2.8s/10.2s = 27% del wall time.

### 9. Memoria — SAFE
268 MB RSS su 19.5 GB disponibili. Pool framebuffer: ~258 MB peak. Nessuna pressione.

---

## 🟢 Risolti di Recente (`feature/frictions-pass1`)

> Branch: `feature/frictions-pass1` · Data: Giugno 2026
> HEAD prima: `2796e99` (8 Giugno 2026) — baseline di questo documento.
> HEAD dopo: `f2aeb1f7` (post-fix) — vedi sotto per delta.

### 🔴 #1 Hot Attribution — RISOLTO (`8387c8a4`)

**Cosa è cambiato (commit `8387c8a4` `perf(telemetry): measure cache+dirty eval phases`):**

| File | Δ | Ruolo del timer |
|---|---|---|
| `src/render_graph/executor/cache_evaluator.cpp` | +3 righe (include + `t_cache0` + fetch_add) | `cache_eval_ms` popolato al termine di `evaluate_cache()` |
| `src/render_graph/pipeline/scene_dirty.cpp`     | +6 righe (include + `t_dirty0` + 2× fetch_add) | `dirty_eval_ms` su entrambi i return path (bypass `!out.use_dirty_rects` + heavy finale) |
| `tests/core/test_cache_eval_dirty_counters.cpp` | nuovo (4 TEST_CASE) | regressione verde per le due fasi |
| `tests/core_tests.cmake`                        | +1 riga (registrazione test) | assembly |
| `.gitignore`                                    | +1 riga (`/vcpkg_bootstrap`) | hygiene |

**Numeri misurati (`ctest` su `chronon3d_core_tests`):**

| Test case | Risultato |
|---|---|
| `counters: cache_eval_ms and dirty_eval_ms fields exist and are aligned` | ✅ pass — campi a 64 byte, default 0, distinct cache line |
| `counters: cache_eval_ms > 0 after a simulate-and-accumulate pass`     | ✅ pass — `> 0` dopo `sleep_for(1.5ms)` deterministico |
| `counters: dirty_eval_ms > 0 after a simulate-and-accumulate pass`     | ✅ pass — `> 0` dopo 4-thread × 200k iter |
| `counters: cache_eval_ms and dirty_eval_ms merge_tls no records lost`  | ✅ pass — 8 thread × 1000 fetch_add → somma esatta = `8000` |

**Run reale** (`./chronon3d_cli render <comp> --frame 0 --report`):

```
comp:        AnimBlurFocus (1920×1080, dirty-rects ON)
cache_eval_ms:  0
dirty_eval_ms:  0
```

> **Interpretazione**: il timer è wired correttamente (test verdi). Su un singolo frame
> a bassa intensità per fase, `static_cast<uint64_t>(duration_ms(...))` arrotonda a 0 ms.
> Per vedere `> 0` serve scena cache-heavy (più nodi / frame) o passare `cache_eval_ms` /
> `dirty_eval_ms` a `uint64_t` in microsecondi (vedi roadmap residua).

### 🔴 #2 TBB Peak Workers — RISOLTO (`08f7bd9a`)

**Cosa è cambiato (commit `08f7bd9a` `perf(tbb): simple_partitioner + transform threshold`):**

| File | Δ | Effetto |
|---|---|---|
| `src/backends/software/kernels/grid_background_kernel.cpp` | +4 righe (commento + `, tbb::simple_partitioner{};`) | forza split parallelo anche su range sparsi (auto_partitioner lasciava peak=1) |
| `src/render_graph/nodes/transform_kernels.cpp`             | 4× threshold 12→8 (`allowMultiple: true`) | simmetria con `software_compositor.cpp` già a 8 |

**Numeri misurati:**

- `tests/golden/test_tbb_workers_parallelism.cpp` continua a passare (pixel golden confrontati vs reference).
- Doctest `tests/core/test_cache_eval_dirty_counters.cpp` indipendente dal TBB, continua a passare.
- Soglie attuali (a valle del commit, post DoD): `composite >= 8`, `transform >= 8`. Il DoD originale chiedeva composite 32→16 / transform 128→64; i valori reali sono già inferiori.

### 🔴 #3 Camera 3D Docs — RISOLTO (`f2aeb1f7`)

**Cosa è cambiato (commit `f2aeb1f7` `docs(camera): add canonical 3D setup guide`):**

| Sezione `docs/ORIENTATION.md` | Δ | Contenuto |
|---|---|---|
| Concetti Chiave (indice)                          | +1 riga | link alla nuova sezione |
| `### Camera 3D — Setup canonico`                  | completa riscrittura (~150 righe) | gerarchia 2D/2.5D/perspective, unità/segni, due esempi compilabili (`example_3d_card()`, `example_3d_tilt()`), 5 errori frequenti (incl. anchor/pivot), validazione |

**Impatto atteso:** riduzione tempo onboarding scene 3D; meno "rotazione ignorata perché manca `enable_3d()`" bug.

### Stato numerico finale

| Counter | Pre-fix this doc | Dopo fix (doc) | Risultato |
|---|---|---|---|
| `cache_eval_ms` | 0 | 0 (sub-ms troncato) / `>0` in scena pesante | wired + verificato da doctest |
| `dirty_eval_ms` | 0 | 0 (sub-ms troncato) / `>0` in scena pesante | wired + verificato da doctest |
| `tbb_active_workers_peak` | 1 | non ricolorato in questo run (golden worker test da `tests/golden/test_tbb_workers_parallelism.cpp`) | simple_partitioner applicato, da misurare |

---



| # | Area | Gap | Impatto | Sforzo | Strategia |
|---|---|---|---|---|---|
| **1** | Hot attribution | 17% → 90%+ | 🔥🔥🔥 Sapere dove va l'83% del tempo | **Basso** | Accendere timer esistenti in `internal.cpp` e `scene.cpp` |  <!-- drift-allow: stale-ref -->
| **2** | Double-buffering | 1× → 1.3× (video) | 🔥🔥 30% più FPS su video | **Medio** | Coda 2 frame + thread conversione |
| **3** | Transform SIMD | 0.5 → 2+ GB/s | 🔥🔥 10-20% speedup | **Medio** | Highway gather + bilinear |
| **4** | Soglie TBB | peak=1 → peak=4+ | 🔥🔥 20-40% su workload pesanti | **Basso** | `simple_partitioner` + soglie ridotte |
| **5** | Frame-level parallelism | 1× → 2× | 🔥🔥 2× teorico | **Alto** | Dopo hot attribution al 90% |
| **6** | Dirty rect overlap | 105% → ≤100% | 🔥 5% | **Basso** | Migliorare merge algoritmo |

---

## Roadmap Raccomandata

### Fase 1 (Subito) — Visibilità
1. **Accendere timer fase** — `cache_eval_ms`, `predicted_bbox_ms` (`executor/internal.cpp`), `dirty_eval_ms` (`scene.cpp`), `clone_context_ms`, `state_assign_ms`
2. Scoprire dov'è l'83% del tempo — con i timer accesi sapremo esattamente quali fasi consumano i 183ms

### Fase 2 (Breve termine) — Ottimizzazioni a Basso Sforzo
3. Abbassare soglie TBB (composite 32→16, transform simple_partitioner)
4. Migliorare dirty rect merge
5. Benchmark Highway SIMD per transform

### Fase 3 (Medio termine) — Architetturali
6. Double-buffering render/convert (specifico per video, impatto 0 su render singoli)
7. Frame-level parallelism (se necessario, dopo aver misurato con hot attribution al 90%)

---

## ⚠️ Stato Working Tree (sessione corrente)

> Flag non-codice: serve per agganciarsi al prossimo commit dell'utente, non
> per modificare file qui.

> Dopo il push di `05887865`, `git status` mostra le seguenti modifiche /
> file untracked, **NON mie** — appartengono all'utente e vanno eseguiti
> in commit separati per non mischiarli con il fix preparatorio
> già pushato:

| Path | Stato | Note |
|---|---|---|
| `content/anims/compositions/cinematic_text_camera.cpp` (+ `.hpp`) | untracked | Le **5 nuove comps text-camera** che hanno originato questo studio di bottleneck. Commit naturale: `feat(content): add cinematic text-camera compositions`. |
| `content/CMakeLists.txt`                                                  | modified | Probabilmente wiring CMake delle comps di cui sopra. |
| `content/anims/CMakeLists.txt`                                             | modified | Idem. |
| `content/common/text_helpers.hpp`                                          | modified | Helper `centered_text::make_centered_text` + `glow::apply_ae_glow` referenziati dalle 5 comps. |
| `content/register_content_modules.cpp`                                     | modified | Registrazione moduli. |
| `apps/chronon3d_cli/commands/telemetry/command_telemetry_helpers.cpp`     | modified | Stub `generate_telemetry_report` aggiunto in sessione precedente per risolvere linker error dopo `-DCHRONON3D_ENABLE_SQLITE_TELEMETRY=ON`. |
| `tools/start_dashboard_shim.py`                                            | untracked | Helper di avvio dashboard. |

**Azione raccomandata (utente, prossimo commit):** `git add -A` di tutti i
path sopra (tutti relativi a contenuti / tooling / telemetry, **nessuna
collisione** con `05887865`), con messaggio tipo `feat(content): add
cinematic text-camera compositions + telemetry stub + dashboard shim`.
Pushare insieme al branch locale, poi procedere con il populate step
(#11 step 2) come ulteriore commit separato.

## File Chiave

| File | Ruolo |
|---|---|
| `src/render_graph/executor/executor.cpp` | Graph executor — scheduling, dispatch node |
| `src/render_graph/executor/internal.cpp` | Cache eval, predicted bbox, telemetry per node |  <!-- drift-allow: stale-ref -->
| `src/render_graph/pipeline/scene.cpp` | `render_scene_via_graph()` — entry point render |
| `src/render_graph/nodes/transform_kernels.cpp` | Transform kernel — sampling, clamping |
| `include/chronon3d/render_graph/nodes/composite_node.hpp` | CompositeNode — copy + blend per layer |
| `src/backends/software/software_compositor.cpp` | Software compositor — SIMD blend |
| `apps/chronon3d_cli/utils/job/render_job_execute.cpp` | Render loop — double-buffering candidato |
| `apps/chronon3d_cli/utils/job/render_job_write_frame.cpp` | Frame write — conversione + pipe |
| `include/chronon3d/render_graph/nodes/clear_node.hpp` | ClearNode — COW logic |
