# TICKET-DAEMON-JOB-LIFECYCLE — il renderer non è lento: lo è il ciclo di vita del job

Stato: **OPEN** (misure PASS, fix NOT RUN)
Contesto: daemon `chronon3d` (IPC RENDER_JOB) + worker RenderingGen.
Origine: misure 2026-09-16 sulla matrice overlay 10×10 e sul probe a due job.

---

## 1. Modello misurato

Il daemon serializza l'esecuzione:

- `apps/chronon3d_cli/daemon/daemon_render_concurrency.hpp:42` —
  `kRenderJobExecutionSerialized = true`, `kSerializedRenderJobSlots = 1`;
- `apps/chronon3d_cli/daemon/daemon_service_ipc.cpp:461` — `m_render_job_mutex`
  tenuto per **tutto** il RENDER_JOB (admission → dispatcher → reply).

Quindi il throughput è `1 / servizio_serializzato`, non `gpu_lanes / servizio`:
sulla matrice 100 overlay il worker aveva 2 lane e `gpu_lane_wait ≈ 0`, ma lo
spacing tra i completamenti era **2.29 s** = un solo job alla volta
(26 overlay/min). `gpu_lanes: 2` nasconde latenza, non aggiunge throughput.

Verifica collaterale: la config live del worker documenta già che `lanes=1` e
`lanes=2` sono indistinguibili (misura 2026-09-13), e l'ambiente del daemon
contiene `CHRONON_RENDER_JOB_SLOTS=2` che **non fa nulla** (constexpr): l'intento
configurato è 2, la capacità effettiva 1 (il daemon lo segnala con un warning).

## 2. Dove va il servizio serializzato (delta tra righe di log, job senza contesa)

| Fase | Costo per overlay | Nota |
|---|---|---|
| render loop 121 frame | 230–258 ms (~500 fps) | **~11% del servizio** |
| pool-warm canvas + pre-import 6 execution slot + encoder open | 610–1 350 ms | **per job**: 101 boot per 100 job |
| gap tra scrittura sidecar e riga `receipt:` | **1 280 ms** | nessun timer lo attribuisce; assente nel percorso single-shot (~250 ms) |
| receipt | 284 ms (probe 282) | + **253 ms** di ri-verifica nel worker |
| handoff al job successivo | 62 ms | |

Somma: 2.2–3.2 s per overlay, di cui **~1.56 s fisso che non produce frame**.

## 3. VRAM per shape (gate della concorrenza)

Probe ripetuto due volte (campionamento `nvidia-smi` a 50 ms, job overlay
FullGraph, 121 frame):

| run | device idle → peak | daemon idle → peak | delta |
|---|---|---|---|
| `vram-probe-1` | 3 525 → 4 117 MiB | 226 → 801 MiB | **592 MiB** |
| `flat-bg-1` | 3 517 → 4 110 MiB | 218 → 793 MiB | **593 MiB** |

La calibrazione globale (~8.8 GB per 1080p FullGraph) citata in
`daemon_render_concurrency.hpp:29-35` **non copre questa shape**. Limite
dichiarato: con il mutex c'è un solo job residente, quindi la misura è il costo
incrementale di **un** job attivo, non la prova che due convivano.

### Harness a due runtime simultanei (eseguito 2026-09-16)

Due `chronon3d_cli render` FullGraph 1080p in parallelo (processi e runtime
distinti, campionamento 50 ms, 84 campioni): idle 3 517 MiB → **peak 6 319 MiB**
(delta 2 802 MiB su 16 376 MiB totali), `render_ms` 1 287/1 201 ms contro
1 071 ms da solo (overlap reale), **artifact bit-identici** tra i due processi.

Conseguenza: la tesi "due exporter 1080p non entrano in 16 GB" è **falsificata**
su questo host per questa shape. Non è ancora l'autorizzazione a rimuovere il
mutex: quei due sono processi cold-start, il daemon tiene pool caldi e la sua
admission carica la VRAM per plan. Serve la stessa misura su 3–5 shape con
telemetria interna degli allocator (exit condition §5).

## 4. Leve, in ordine di leva

1. **Attribuire e togliere i 1 280 ms** (oggi invisibili a tutti i timer) —
   candidati da tagliare con timer dedicati: drain/chiusura encoder, finalize
   del mux, `vkDeviceWaitIdle`/fence wait, sync CUDA, distruzione di execution
   slot/pool, hash + rename atomico + sidecar. Un solo `waitIdle` o un poll con
   backoff in quel tratto spiegherebbe il costo.
2. **Una sola verifica dell'artifact**: il daemon produce già il receipt
   canonico; il worker lo ri-verifica con un secondo `ffprobe` (253 ms). La
   forma corretta è un'unica authority `ArtifactReceipt` consumata dal worker,
   con il secondo probe come modalità audit.
3. **Smontare il costo per-job**: riusare device/runtime, contesto Vulkan/CUDA,
   canvas pool, execution slot, atlas/cache e staging buffer tra output
   indipendenti. **Non** partire dall'encoder H.264 (DPB/RC/header implicano
   contaminazione temporale).
4. **Batch daemon job**: `1 runtime + 1 pool + 1 preparation → N output
   indipendenti` con K lane bounded, invece di N × (setup + warm + teardown +
   receipt). Sulla stessa GPU è la leva più grande, prima di alzare K.
5. **Solo dopo**: K da 1 a 2 dietro un tetto VRAM misurato per shape (l'harness a
   due runtime simultanei, §5).

## 5. Exit condition della domanda "si può togliere il mutex?"

Un harness di laboratorio che tiene **due runtime della stessa shape vivi
contemporaneamente** (due daemon su socket distinti, o due render CLI in
parallelo), misurando peak device memory con campionamento strumentato (non
50 ms) e ripetendo su 3–5 shape: overlay-only, video-sorgente FullGraph,
DirectYUV, 4K. Finché quel numero non esiste, la decisione di rimuovere
`m_render_job_mutex` resta non presa — `derive_vram_working_set` va **alimentato**
da quella calibrazione, non sostituito.

## 6. Ripro

- probe VRAM + submit: `RenderingGen/multilingual_overlays_v1/scripts/vram_probe.py`
  (evidenze: `.../evidence/vram_overlay_shape_2026-09-16.json`);
- budget serializzato: `journalctl -u chronon3d` con i delta tra
  `[pool-warm] Pre-allocated and pre-imported`, `Opened native encoder`,
  `[video] render_ms=`, `Wrote result.mp4`, `receipt:`.
