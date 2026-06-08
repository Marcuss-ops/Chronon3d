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

**Perché i counter sono a zero:** I timer di sottofase non vengono popolati perché `ctx.timer.start()`/`.stop()` non è mai chiamato per quelle fasi in `src/render_graph/executor/internal.cpp`.

**Come superarlo:**
1. Aggiungere `ctx.timer.start()` / `.stop()` per ogni fase del graph executor:
   - `src/render_graph/executor/internal.cpp` → `evaluate_cache()`, `predicted_bbox()`
   - `src/render_graph/executor/executor.cpp` → node scheduling, dispatch
   - `src/render_graph/pipeline/scene.cpp` → dirty rect, input resolve, clone context
2. Modificare ~10 righe in 3 file per accendere i timer
3. Risultato atteso: attribuzione → **90%+**, scopriamo esattamente dove vanno i 183ms

**File da modificare:**
- `src/render_graph/executor/internal.cpp` (~riga 30: cache eval → `timer.start/stop("cache_eval_ms")`)
- `src/render_graph/executor/internal.cpp` (~riga 50: predicted bbox → `timer.start/stop("predicted_bbox_ms")`)
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

## Priorità di Ottimizzazione

| # | Area | Gap | Impatto | Sforzo | Strategia |
|---|---|---|---|---|---|
| **1** | Hot attribution | 17% → 90%+ | 🔥🔥🔥 Sapere dove va l'83% del tempo | **Basso** | Accendere timer esistenti in `internal.cpp` e `scene.cpp` |
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

## File Chiave

| File | Ruolo |
|---|---|
| `src/render_graph/executor/executor.cpp` | Graph executor — scheduling, dispatch node |
| `src/render_graph/executor/internal.cpp` | Cache eval, predicted bbox, telemetry per node |
| `src/render_graph/pipeline/scene.cpp` | `render_scene_via_graph()` — entry point render |
| `src/render_graph/nodes/transform_kernels.cpp` | Transform kernel — sampling, clamping |
| `include/chronon3d/render_graph/nodes/composite_node.hpp` | CompositeNode — copy + blend per layer |
| `src/backends/software/software_compositor.cpp` | Software compositor — SIMD blend |
| `apps/chronon3d_cli/utils/job/render_job_execute.cpp` | Render loop — double-buffering candidato |
| `apps/chronon3d_cli/utils/job/render_job_write_frame.cpp` | Frame write — conversione + pipe |
| `include/chronon3d/render_graph/nodes/clear_node.hpp` | ClearNode — COW logic |
