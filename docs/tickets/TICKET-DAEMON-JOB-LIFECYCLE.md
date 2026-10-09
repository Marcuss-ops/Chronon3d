# TICKET-DAEMON-JOB-LIFECYCLE — il renderer non è lento: lo è il ciclo di vita del job

**Aggiornamento 2026-10-08 — calibrazione VRAM** (`RenderingGen/multilingual_overlays_v1/evidence/vram_calibration_suite_2026-10-08.json`, `complete=true`, 3 shape × 3 run, artefatti identici e device coerente in tutte le ripetizioni):

- overlay 1080p: picco device 9854 MiB, attribuito al daemon 4940 MiB;
- video-source 1080p: picco device 8828 MiB, attribuito 3956 MiB;
- overlay 4K: picco device 14536 MiB, attribuito 9621 MiB.

**Decisione:** `m_render_job_mutex` **resta**. Due job 4K concorrenti (~2 × 9.6 GB attribuiti) superano i 16 GB della RTX A4000; la rimozione richiede prima un controllo di budget VRAM in ammissione, non ancora implementato. La matrice 2026-10-08 sostituisce quella del 2026-09-17, i cui piani non sono più caricabili (vedi commento nel manifest).

Stato: **OPEN** — temporal schedule, bake delle static island e IPC
`RENDER_BATCH` sono ora collegati al runtime e compilano; restano la misura GPU
multi-shape, la decisione runtime sul mutex e il riuso del lifecycle encoder
tra job nel batch.
Contesto: daemon `chronon3d` (IPC RENDER_JOB) + worker RenderingGen.

## Implementazione verificabile locale — static island / temporal / batch

- `CompiledProgram` possiede uno `TemporalUpdateSchedule` per nodo e proprietà;
  lo sampler scrive il dirty bit e `SceneRefresh` salta il refresh dei binding
  invariati. Cue e keyframe conservano i propri eventi/intervalli; contenuto video
  e particle sono per-frame.
- `CompiledFrameProgram` emette le isole statiche, le loro chiusure e una
  superficie persistente al root. Dopo il primo render, l'executor riusa il root
  e salta i membri quando tutte le isole sovrapposte sono pronte; al confine del
  job il renderer invalida i bake runtime senza buttare il grafo compilato.
  Nessuna cache viene accreditata prima che il root abbia prodotto una superficie.
- IPC `RENDER_BATCH` decodifica e valida fino a 64 richieste prima di avviarle,
  quindi le esegue in ordine sul daemon e riusa renderer caldo e runtime registry.
  Un gate esclusivo impedisce a un RENDER_JOB singolo di inserirsi tra due
  elementi del batch. Il percorso video riceve ora lo stesso renderer per-device
  del daemon e conserva graph/pipeline/font cache tra job, azzerando lo stato
  temporale. Per export FullGraph NVENC, il daemon conserva anche
  `GpuSlotPool`, superfici importabili e `TripleBufferArena` per device e
  contratto canvas/pixel; il pool viene drenato per job e ricreato se cambia il
  contratto. Codec context e muxer restano per-output.
- Evidenza locale precedente (supersata dall'aggiornamento 2026-10-07):
  architecture gate PASS 206/206.
  La build fresca del target temporal a `-j8` ha superato il timeout host di 600 s;
  il tentativo di ripresa a `-j8` è stato interrotto al rilevamento di un altro
  build `-j16` concorrente per non superare il limite globale (267/368 azioni).
  Non viene registrata come PASS e il test binario aggiornato non è disponibile.
  Quella build e quelle verifiche sono superate dai risultati runtime riportati
  sotto; pixel parity GPU e benchmark di throughput restano OPEN/NOT RUN.

### Aggiornamento 2026-10-07 — rate analysis locale

- `TemporalRateAnalysis` produce una classe per layer (`Static`, `Cue`,
  `KeyframeInterval`, `PerFrame`, `Subframe`), frame valutabili limitati e
  schedule per proprietà. Il report CLI/sidecar è consultivo; il corrispondente
  `TemporalUpdateSchedule` compilato viene invece consumato dallo sampler e da
  `SceneRefresh`. I segmenti lineari enumerano i frame intermedi, `hold` e i
  cue emettono i confini.
- La correzione cue-boundary è compilata in `chronon3d_c_abi_tests`; il target
  completo `chronon3d_dev_fast` e i target `chronon3d_c_abi_tests` e
  `chronon3d_cli_tests` sono stati compilati/linkati con `-j12` nel profilo
  `linux-fast-dev`. I test non sono stati eseguiti in questo aggiornamento.
  Con `linux-video-test`, il serializer della sidecar e il relativo test schema
  compilano come translation unit; il binario video completo non è stato linkato.
- È stato aggiunto un planner/esecutore per-frame del Variant DAG con fingerprint
  distinti per base visiva, layout e audio; non è ancora collegato al percorso
  whole-video exporter e non è accreditato come batch whole-video.
- PixelProgram SPIR-V/GPU lowering rimane non implementato. La build conferma
  solo compilazione/link; pixel parity GPU, esecuzione test e benchmark batch
  restano da raccogliere.

### Chiusura implementativa locale — 2026-10-07

- **Voci 2–4 (temporal, static bake, batch runtime): implementazione presente.**
  Lo schedule temporale è collegato a sampler/refresh; `GraphExecutor` conserva
  l'output della chiusura statica dopo il primo render e ne riusa la superficie
  persistente; IPC `RENDER_BATCH` serializza l'intera sequenza mentre renderer e
  cache per-device restano caldi tra gli output. I test GPU multi-shape, la
  calibrazione allocator e la decisione sul mutex restano criteri distinti di
  certificazione. Il codec context e il muxer restano per-output.
- Il decode `RENDER_BATCH` ora vive nel protocol codec: validazione batch e
  decode dei singoli job avvengono prima del lock e dell'esecuzione. Il parsing
  JSON testuale è condiviso con `RENDER_JOB`; il gate architetturale passa
  206/206 il 2026-10-07.
- **Voce 6 (PixelProgram CPU fusion): implementazione presente.** Il payload
  `DeviceProgram` CPU v2 ora serializza e ricostruisce stack pointwise completi,
  preservando fingerprint, determinism class e gate di certificazione. Il test
  `chronon3d_pixel_program_tests` passa 24/24 casi e 96/96 assert il 2026-10-07.
  La GPU lowering SPIR-V resta esplicitamente non implementata; non è stata
  richiesta né dichiarata come certificata su questo host.
- Una build ampia di `chronon3d_dev_fast` si è fermata durante `ranlib` con
  `No space left on device` (3.1 GB lib statiche, 3.2 GB liberi). Il target
  isolato PixelProgram ha compilato, linkato ed eseguito con successo. Le unità
  IPC e il relativo test batch compilano; il binario CLI completo non è stato
  rilinkato (2.3 GB liberi dopo la compilazione).

### Aggiornamento 2026-10-07 — runtime batch persistente

- `RENDER_BATCH` ora possiede il render-domain gate per l'intera sequenza; i
  render singoli prendono il lato condiviso e attendono la fine del batch.
- Il `warm_renderer` daemon, prima usato dai render still ma ignorato dal
  percorso video, viene inoltrato fino a `setup_pipe_export_session`; gli export
  FullGraph riusano il renderer, le graph/pipeline/font cache e il runtime CUDA
  per-device. Frame values e temporal history vengono ripuliti a ogni job.
- La build `cmake --build build/chronon/linux-fast-dev -j12 --target
  chronon3d_dev_fast chronon3d_c_abi_tests chronon3d_cli_tests` è PASS; i test
  non sono stati eseguiti. Il codec context FFmpeg/NVENC e il muxer restano
  scoped al singolo file di output; la persistenza del runtime hardware non
  equivale a mantenere aperto lo stesso encoder per output indipendenti.

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
| gap tra scrittura sidecar e riga `receipt:` | **1 280 ms (misura storica)** | aggiunti timer sidecar→receipt e receipt-write il 2026-10-05; campione daemon aggiornato ancora da raccogliere |
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

1. **Attribuire e togliere i 1 280 ms** — il percorso ora logga
   `sidecar_write_ms`, `session_teardown_ms`, `post_sidecar_to_receipt_ms`,
   `receipt_build_verify_ms` e `receipt_file_write_ms`. Il delta storico non è
   ancora stato riprodotto dopo questa strumentazione; il prossimo campione
   deve conservare le cinque misure e i confini `Wrote result.mp4` / `receipt:`
   per distinguere drain, finalize, sync, teardown, verifica e scrittura del
   receipt.
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
   Inventario precedente del checkout: **113 piani concreti, tutti 1920×1080**.
   È stato aggiunto `tools/fixtures/vram-calibration-4k-overlay.plan.json`
   (3840×2160, color + testo, 48 frame). La validazione CLI è PASS (`content_digest`
   `4df908b0d4578545e27e6b1c2b6d36045500bed49f41ff44110bc4fef36435af`), ma il
   piano non è ancora incluso/eseguito dalla suite VRAM né calibrato su GPU.
   Restano quindi le 2 shape misurate (overlay-only, video-sorgente), la shape
   4K autorata ma non misurata, e DirectYUV ancora da certificare estraendo
   `execution_path` dal run invece di dichiararlo in una label;
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
