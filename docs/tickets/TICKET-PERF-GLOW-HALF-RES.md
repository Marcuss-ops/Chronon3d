# TICKET-PERF-GLOW-HALF-RES — il glow è l'unico costo GPU reale, ed è fill-bound

**Stato:** PROPOSTA, NON AVVIATO. Si attiva solo dopo
[`TICKET-PERF-FRAME-PROOF-CHEAP`](TICKET-PERF-FRAME-PROOF-CHEAP.md) e
[`TICKET-PERF-TIMELINE-FLATBUFFER`](TICKET-PERF-TIMELINE-FLATBUFFER.md): oggi la
GPU è l'**8,6%** del frame (6,6% di utilizzazione media), quindi ottimizzarla non
muove il throughput.

## TODO — cosa manca (agg. 2026-11-01)

- [ ] **P1 — Soglia di attivazione, non entusiasmo.** Logica: la GPU diventa il
  collo solo quando il frame CPU scende sotto il costo GPU. Condizione esplicita:
  frame medio **≤6 ms** su corpus overlay. Fino ad allora questo ticket resta
  NOT RUN. Gate: sidecar che mostra `device_span` ≥50% del frame.
- [ ] **P2 — Glow a risoluzione ridotta.** Logica: `try_native_full_frame_glow_or_bloom`
  esegue 4 passate box-blur sull'intera superficie 1920×1080 per un alone che si
  legge a 1080p con raggio ≤64 px; il fill è il costo, non l'ALU. Servono down
  a 1/2 (o 1/4) lineare, blur alla scala ridotta, up nella composizione. Dove:
  `src/render_graph/nodes/effect_stack_node_native_detail.cpp:33-190`
  (`try_native_full_frame_glow_or_bloom`, `glow_surfaces(horizontal, vertical, …)`
  a `:166-168`), `src/backends/vulkan/vulkan_backend_operations_private.cpp`
  (passate `blur_surface`). Gate: `device_span` del glow da **2,7 ms a ≤0,9 ms**
  e PSNR ≥40 dB / SSIM ≥0,99 contro il rendering attuale su 20 frame di
  riferimento; se il look cambia, è una decisione editoriale e si chiude con
  approvazione, non con un numero.
- [ ] **P3 — Misurare il tetto, non stimarlo.** Logica: la capacità GPU per
  overlay/min dipende dal mix di effetti del singolo job; una curva certificata
  richiede 3 run per configurazione. Dove: `TICKET-GPU-BASELINE-CORPUS` (corpus
  attivo assente in questo checkout — il gate resta WAITING EXTERNAL). Gate:
  item/min a GPU satura misurati, non proiettati.

## Evidenza misurata (r49)

| Voce | Valore | Quota |
|---|---:|---:|
| `gpu_execute_ms` (batch) | 3 027,4 ms | 4,1% del render loop |
| GPU per frame reso | **3,97 ms** | — |
| └ glow (955 passate) | **2,7 ms** | **68% del tempo GPU** |
| `gpu_utilization_avg` | **6,6%** (picco 44%) | — |
| `render_loop_wall_ms` / frame | 74 257,8 ms / 1607 | 46,3 ms/frame |
| `timeline_eval_ms` | **68 306,5 ms** | **92% del loop** |
| VRAM picco | 7 616 MB / 16 376 | — |

Con la composizione attuale (73 frame di timeline e 34,7 frame resi per item da
3,62 s; 3,97 ms GPU/frame reso = **138 ms GPU/item**) una RTX A4000 satura a
**~430 overlay/min**. È esattamente il bordo del 500/min: il target non è
comodo, è il 100–105% di una GPU. Togliendo o dimezzando il glow, lo stesso
hardware arriva a **~1.800–2.300 item/min** teorici, e il limite torna dove deve
stare (la CPU a ≤8 ms/frame, poi più lane).

## Non obiettivi

- **Shader scritti a mano / pipeline per animazione**: sbagliano il bersaglio.
  L'ALU non è il costo (6,6% di utilizzazione GPU media, 8 264 passate
  `composite` e 955 `glow` senza contesa); una pipeline per animazione aggiunge
  compilazioni (stutter), VRAM e manutenzione per spostare 4 ms su 46.
- **Ridurre la risoluzione di output**: il fill totale è 4% del frame; il
  problema è solo il raggio grande del glow.
- **Toccare l'encoder**: 991,7 ms sull'intero batch (1,3% del loop), di cui
  130,1 ms di flush a fine job; non c'è nulla da prendere.
- **Profondità del ring / slot pool**: provato, 232–239 fps contro una banda di
  varianza; nessun movimento (N2 e N6 di
  [`100x-IDEE-NUOVE-2026-10-31.md`](../../../100x-IDEE-NUOVE-2026-10-31.md)).

## Decisione collegata

[`ADR-DRAFT-DECIDE-BEFORE-EVALUATE`](ADR-DRAFT-DECIDE-BEFORE-EVALUATE.md) —
questo ticket è l'unico che *aggiunge* lavoro GPU invece di toglierlo, per
questo ha una condizione di attivazione e non una priorità.
