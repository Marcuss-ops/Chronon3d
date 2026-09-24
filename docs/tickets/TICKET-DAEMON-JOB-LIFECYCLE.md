# TICKET-DAEMON-JOB-LIFECYCLE — il renderer non è lento: lo è il ciclo di vita del job

Stato: **OPEN** — la sincronizzazione di lifecycle/admission è ora RAII e
race-free, e l'allocator telemetry contract è atterrato; restano la misura GPU
multi-shape, la decisione runtime sul mutex, il batch one-runtime/one-pool e
il ~1 s intermittente del percorso di produzione.
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

### Harness a due runtime simultanei

**(a) CLI cold-start** (eseguito 2026-09-16, evidenza
`vram_two_runtime_2026-09-16.json`): due `chronon3d_cli render` FullGraph 1080p in
parallelo (processi e runtime distinti, campionamento 50 ms, 84 campioni): idle
3 517 MiB → **peak 6 319 MiB** (delta 2 802 MiB), `render_ms` 1 287/1 201 ms
contro 1 071 ms da solo, artifact bit-identici.

**(b) due pool caldi posseduti** (eseguito 2026-09-16, evidenza
[`docs/evidence/vram-two-runtime-pool-2026-09-16.md`](../evidence/vram-two-runtime-pool-2026-09-16.md),
harnais ripetibile `cmd/vram-probe -mode two-runtime-pool`): due daemon propri su
socket privati, un render FullGraph 1080p/120 concorrente ciascuno, campionamento
20 ms, fasi etichettate idle/warm/rendering/resident-hold/shutdown:

| fase | device `memory.used` | attribuito a `chronon3d_cli` |
|---|---|---|
| idle (1 daemon di sistema) | 332 MiB | 248 MiB |
| warm pools (2 runtime in più, fermi) | 334 MiB | 248 MiB |
| rendering (2 job in volo) | **1 945 MiB** | **1 814 MiB** |
| resident hold (pool vivi) | 768 MiB | ~644 MiB |

Artifact **byte-identici** (`02c25c77d965…`). Tre run consecutivi danno
l'attribuzione per processo stabile (1 692–1 814 MiB per due render) mentre il
contatore **device-wide** varia di oltre 3× (1 473 → 5 281 MiB di delta) perché
l'host è condiviso: `memory.used` non è una misura di questa suite. Due correzioni
al modello implicito: un pool caldo che non ha ancora ammesso un job **non tocca
il device** (allocazione lazy → costo residente letto in `resident_hold`), e la
calibrazione va fatta su attribuzione per processo / telemetria interna, non su
`memory.used`.

Conseguenza: la tesi "due exporter 1080p non entrano in 16 GB" è **falsificata**
su questo host per questa shape, sia con processi cold-start sia con due pool
caldi. Non è ancora l'autorizzazione a rimuovere il mutex: mancano le 3–5 shape
certificate e la telemetria interna degli allocator (exit condition §5).

## 4. Leve, in ordine di leva

1. **Attribuire e togliere i 1 280 ms** (oggi invisibili a tutti i timer) —
   candidati da tagliare con timer dedicati: drain/chiusura encoder, finalize
   del mux, `vkDeviceWaitIdle`/fence wait, sync CUDA, distruzione di execution
   slot/pool, hash + rename atomico + sidecar. Un solo `waitIdle` o un poll con
   backoff in quel tratto spiegherebbe il costo.
   **Aggiornamento 2026-09-17 (secondo esemplare della stessa classe, trovato e
   chiuso):** la ricerca statica di un worker periodico `join`-ato da un
   distruttore sul percorso del job ha trovato `NvmlSampler`
   (`utils/telemetry/nvml_sampler.hpp`), una **locale di `video_export_pipe`**
   avviata con intervallo 250 ms e fermata a riga 216: la vecchia `stop()`
   attendeva la fine del tick (forma vecchia misurata: p50 **250 ms**, max
   250 ms su n=20) e un sample NVML in volo. Ora l'attesa è interrompibile e il
   costo residuo è **misurato** (`NvmlStats::sampler_stop_ms`, stampato come
   `nvml_stop_ms=` nella riga `[video]`); dopo il fix: p50 **0.12–0.14 ms** nei
   due casi peggiori (n=20 ciascuno). Evidenza e numeri:
   [`docs/evidence/nvml-sampler-teardown-2026-09-17.md`](../evidence/nvml-sampler-teardown-2026-09-17.md).
   **L'~1 s intermittente resta non attribuito**: su questo host le query NVML
   costano 0.12–0.26 ms, quindi il sospetto successivo restano `encoder_reset` /
   `full_graph_reset` sul percorso di produzione.
2. **Una sola verifica dell'artifact** — **ATTERRATA** (verificata nel codice,
   2026-09-17). Il daemon produce il receipt canonico; il worker
   (`RenderingGen/renderinggen/internal/processor/finalize_job.go`) lo consuma:
   `probeFactsForFinalize` mappa il receipt in `media.ProbeResult`
   (`probeResultFromReceipt`) e invoca `media.ProbeFile` **solo** quando il
   receipt non è canonico/leggibile — con un log esplicito quando succede, così
   il fallback non è mai silenzioso. `verifyArtifactReceipt` applica poi la
   policy e l'identità byte del receipt, e il worker non ri-decodifica l'output
   una seconda volta (`finalize_job.go`: "it never re-decodes the output a
   second time"). Il secondo probe resta quindi fallback/audit, non percorso
   normale: la voce `worker duplicate verification ~250 ms` del §7 non descrive
   più il percorso caldo.
3. **Smontare il costo per-job**: riusare device/runtime, contesto Vulkan/CUDA,
   canvas pool, execution slot, atlas/cache e staging buffer tra output
   indipendenti. **Non** partire dall'encoder H.264 (DPB/RC/header implicano
   contaminazione temporale).
   **Aggiornamento 2026-09-16 (misurato, non inferito):** sul percorso a due pool
   caldi l'intera coda di vita è attribuita e vale ~174 ms per job
   (`encoder_reset` 43–46 ms + `full_graph_reset` 127–132 ms + `sidecar_write`
   11 ms; `slot_pool_close`/`surface_release`/`queue_close`/`writer_join`/
   `decoder_reset` a 0). Quindi il ~1 s **intermittente** non è il teardown del
   daemon su questa shape: va cercato nel percorso serializzato di produzione o
   nella coda del **worker**, con i timer già esistenti — vedi
   [`docs/evidence/vram-two-runtime-pool-2026-09-16.md`](../evidence/vram-two-runtime-pool-2026-09-16.md) §4.2.
4. **Batch daemon job**: `1 runtime + 1 pool + 1 preparation → N output
   indipendenti` con K lane bounded, invece di N × (setup + warm + teardown +
   receipt). Sulla stessa GPU è la leva più grande, prima di alzare K.
5. **Solo dopo**: K da 1 a 2 dietro un tetto VRAM misurato per shape (l'harness a
   due runtime simultanei, §5).

## 5. Exit condition della domanda "si può togliere il mutex?"

**Repository closeout (2026-09-20):** the daemon admission context now owns the
policy-selected lock and scheduler reservation through RAII, shutdown observes
`m_running` atomically, and VMA allocator snapshots are carried into the video
GPU timing sidecar. These changes make the gate fail-closed and measurable; they
do not authorize removing the mutex without the external 3–5-shape GPU suite.

L'harness di laboratorio che tiene **due runtime della stessa shape vivi
contemporaneamente** (due daemon posseduti su socket distinti, campionamento
20 ms, fasi etichettate) è ora **atterrato e riproducibile**:
`cmd/vram-probe -mode two-runtime-pool` + `internal/vramprobe/pool.go`,
evidenza in [`docs/evidence/vram-two-runtime-pool-2026-09-16.md`](../evidence/vram-two-runtime-pool-2026-09-16.md).

Resta aperto, e sono i criteri di accettazione della decisione:

1. **3–5 shape** misurate con lo stesso harness: overlay-only (fatto, §3b),
   video-sorgente FullGraph, DirectYUV, 4K, ed eventualmente 3D. **Stato
   2026-09-17:** l'esecuzione è ora una **suite** (matrice versionata, ≥3 run
   per shape imposti dal loader, aggregazione min/p50/max dell'attribuzione per
   processo, dry-run senza GPU) — `cmd/vram-probe -mode calibration-suite`,
   [`docs/evidence/vram-calibration-suite-2026-09-17.md`](../evidence/vram-calibration-suite-2026-09-17.md).
   Inventario del checkout: **113 piani concreti, tutti 1920×1080** → 2 shape
   eseguibili (overlay-only, video-sorgente) e due bloccate da cause diverse:
   **4K** non ha alcun piano nel repository, **DirectYUV** non è una proprietà
   del piano (va certificata estraendo `execution_path` dal run, non dichiarata
   in una label);
2. **telemetria interna degli allocator** al posto di `nvidia-smi`: su host
   condiviso il contatore device-wide non è stabile (§3b), quindi il numero che
   alimenta `derive_vram_working_set` deve venire dal runtime;
3. **ripetizione** (≥3 run per shape) con attribuzione per processo, perché un
   singolo campione non è una calibrazione;
4. solo allora la decisione su `m_render_job_mutex`, e `derive_vram_working_set`
   va **alimentato** da quella calibrazione, non sostituito.

## 6. Ripro

- probe VRAM singolo (percorso queue, un job residente):
  `cd RenderingGen && renderinggen/bin/vram-probe -mode single -source-manifest <manifest certificato> -out <report>`
  (comando: `renderinggen/cmd/vram-probe`, logica: `renderinggen/internal/vramprobe/probe.go`;
  evidenza: `RenderingGen/multilingual_overlays_v1/evidence/vram_overlay_shape_2026-09-16.json`);
- harness a due runtime/pool caldi (fasi + campionamento 20 ms):
  `renderinggen/bin/vram-probe -mode two-runtime-pool -plan <render-plan.json> -chronon-bin <chronon3d_cli> -assets-root <asset root assoluto> …`
  (logica: `internal/vramprobe/pool.go`; evidenza: `.../vram_two_runtime_pool_2026-09-16.json` + `.samples.log`).
  Nota: l'asset root passato a un daemon **deve** essere assoluto — l'`AssetResolver`
  rifiuta un path relativo; l'harness ora lo risolve prima di avviare i pool.
- budget serializzato: `journalctl -u chronon3d` con i delta tra
  `[pool-warm] Pre-allocated and pre-imported`, `Opened native encoder`,
  `[video] render_ms=`, `Wrote result.mp4`, `receipt:`.

> Le vecchie istruzioni indicavano `RenderingGen/multilingual_overlays_v1/scripts/vram_probe.py`:
> quel file non esiste in questo checkout (la directory `scripts/` non c'è). Gli entry
> point reali sono quelli sopra.

## Checkout update — 2026-09-24

- RENDER_JOB JSON decode remains in `daemon_service_ipc_protocol.cpp`; the
  full admission/execution handler was extracted from the IPC switch into
  `daemon_service_ipc_render_job.cpp`. The switch now delegates by payload and
  does not own render-job policy. `ASSEMBLE_SEGMENTS` JSON ingress is also in
  the protocol codec, and Wave9 pins normalization to the new canonical IPC
  boundary while pinning JSON parse sites to the codec.
- `RenderJobAdmissionContext` releases the execution lock and device reservation
  on unwind. Its CLI test transfers the reservation into the same
  `VideoJobExecutionContext` passed by production, throws from a by-value
  dispatcher test double, and checks the moved-from caller, device credits, and
  subsequent lock acquisition afterward.
- The rebuilt CLI suites pass (79/79 CLI cases; 20/20 render-job contract
  cases), including the throwing-dispatcher reservation test. The 197-rule
  architecture gate passes against this extraction, including Wave9's canonical
  decode/normalization boundaries. This is host-free code verification only;
  allocator calibration and the 3–5 GPU shape suite in §5 remain NOT RUN here.
