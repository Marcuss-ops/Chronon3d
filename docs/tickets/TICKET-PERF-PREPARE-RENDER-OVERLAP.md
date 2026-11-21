# TICKET-PERF-PREPARE-RENDER-OVERLAP — il prepare asset non deve stare dentro il lock GPU

**Stato:** PARTIAL — aggiornato 2026-11-21. Read/parse/compile del RenderPlan e callback di prepare asset CPU per N+1 sono prima dell'admission e verificati dal test IPC. A/B locale sul corpus caption + `entity-chain-bg` r21 con due producer, tre run e output byte-identici: batch 10 +6,6%, batch 20 +3,9%, batch 100 −1,1% item/min. I timestamp provano il prepare N+1 durante il frame loop N; il primo item N+1 registra 5 hit/0 miss. Sui PNG mappa approvati (20 item × 3 run) resta negativo: 42,06 vs 54,71 item/min software (−23,1%) e 40,25 vs 41,49 Vulkan (−3,0%), hash identici. Il picco VRAM sidecar sui batch lunghi 100 è 9.711 MiB off / 9.673 MiB on; sui batch 10/20 la differenza cambia segno (−30/+166 MiB a favore di off/on), senza una baseline host quiescente che certifichi margine assoluto. Il prefetch resta opt-in globale perché il guadagno non si mantiene sul batch lungo e mappe. Restano il replay P5 su pool/host accettazione (G5.4) e baseline VRAM controllata. Report: [`prepare_overlap_prefetch_20261121`](../../../ops/jobs/prepare_overlap_prefetch_20261121/REPORT.md) e [`throughput_map_assets_20261121`](../../../ops/jobs/throughput_map_assets_20261121/REPORT.md).
**Area:** `apps/chronon3d_cli/daemon/daemon_service_ipc_render_job.cpp`, `apps/chronon3d_cli/commands/video/exporters/pipe_export_pipeline_setup.cpp`, `src/runtime/render_preparation_assets_detail.hpp`.
**Owner:** maintainer daemon job runtime + pipeline video.
**Dipende da:** [`TICKET-PERF-ASSET-DECODE-HOTPATH`](TICKET-PERF-ASSET-DECODE-HOTPATH.md) per la parte di decode; questo ticket è indipendente per la parte di scheduling.
**Non sostituisce:** [`TICKET-DAEMON-JOB-RUNTIME`](TICKET-DAEMON-JOB-RUNTIME.md) (prepared-plan LRU e lifecycle) né [`TICKET-PERF-ASSET-DECODE-HOTPATH`](TICKET-PERF-ASSET-DECODE-HOTPATH.md).

**Aggiornamento worker P5 (2026-11-21):** i run 6/7/8 del path RenderingGen
queue → worker locale hanno completato 100 job ciascuno, mediana 196,896
item/min (+19,9% rispetto ai precedenti 164,185), zero retry e hash identici
per tutti i 100 indici con `no_dirty_rects=true`; G5.1/G5.2/G5.3 PASS sul mix
locale. G5.4 non è certificato (pool completo e host quiescente non disponibili).
La preparazione immagine non era nel workload, quindi resta il replay worker
r2/r3 dedicato al decode/overlap. Dettagli: [`worker P5 2026-11-21`](../../../ops/jobs/chronon3d_worker_p5_20261121/REPORT.md).

---

## 0. Evidenza

`[CODICE]` `DaemonService::handle_render_job_request` crea il `RenderJobAdmissionContext` **prima** di chiamare il dispatcher:

- `apps/chronon3d_cli/daemon/daemon_service_ipc_render_job.cpp:136-150`: `admission.emplace(...)` acquisisce il lock di esecuzione e la reservation.
- `apps/chronon3d_cli/daemon/daemon_service_ipc_render_job.cpp:211-214`: il dispatcher esegue il job.
- `apps/chronon3d_cli/daemon/daemon_service_ipc_render_job.cpp:245`: `admission.reset()` arriva **dopo** render, mux e chiusura encoder.

`[CODICE]` Dentro il dispatcher, `setup_pipe_export_session` chiama `runtime::prepare_render(...)` (`apps/chronon3d_cli/commands/video/exporters/pipe_export_pipeline_setup.cpp:500-503`), che a sua volta decodifica le immagini (`src/runtime/render_preparation_assets_detail.hpp:143-197`). Quindi il decode di 1,9–3,7 s per item (vedi il ticket asset) è serializzato contro il render GPU del job precedente.

`[MISURA]` Nei job caption del 2026-11-19 il render loop è 2,4–4,3 s e l'asset decode è 1,9–3,7 s: sono due fasi quasi della stessa durata, ma oggi non si sovrappongono mai. Fonte: `ops/jobs/entity_rect_caption_20261119/**/*-full.json`, `chronon_telemetry.job.prepare.asset_decode_ms` e `render_loop_wall_ms`.

`[INFERITO]` Con `LanesPerDaemon=2` la seconda lane può già sovrapporre la receipt alla render successiva, ma **non** il prepare: l'`admission.reset()` che libera il lock è dopo il dispatcher, e il dispatcher contiene il prepare. La seconda lane aspetta il lock per l'intero prepare+render.

---

## 1. Obiettivo

Separare il job in due domini:

1. **Preparation** — lettura/parse/decode/validate/compile del piano, decode e conversione CPU delle immagini, risoluzione font, costruzione del `PreparedRenderPlanContext`. Non crea un renderer, non apre l'encoder, non alloca le surface frame-transient.
2. **Execution** — promozione GPU degli asset, apertura encoder, render, mux, receipt. Resta l'unico dominio che può toccare Vulkan/CUDA/NVENC e le surface.

Il prepare del job N+1 deve poter girare mentre il job N è in execution, senza creare un secondo renderer e senza cambiare l'output.

Non è un secondo scheduler GPU e non è un secondo registry: è la stessa `RenderJobAdmissionContext` con due fasi e due limiti espliciti.

---

## 2. Piano (split)

### Fase A — Misurare dove finisce il prepare

- Aggiungere al sidecar i tempi separati: `plan_prepare_ms`, `image_decode_cpu_ms`, `image_convert_cpu_ms`, `gpu_promote_ms`, `font_prepare_ms`, `encoder_open_ms`.
- Misurare un batch di 10 item con lo stesso payload e `LanesPerDaemon=2`.
- Riportare `daemon_admission_wait_ms` per item: oggi è ~0,002 ms nei job r2/r3, quindi il lock non è conteso perché il producer è seriale; il gate deve usare un producer concorrente per vedere l'overlap.

**Gate A:** i campi sono presenti in 3 run; la somma delle fasi si riconcilia con `prepare_ms`; il tempo di attesa admission è misurato con almeno 2 job in coda.

### Fase B — Prepare CPU fuori dal lock GPU

- Spostare `prepare_render_plan` (read/parse/decode/validate/compile) prima dell'`admission.emplace`, in uno slot di preparazione bounded (es. `prepare_slots=1` per device, o un limite CPU esplicito).
- Estrarre dal `prepare_render` la parte CPU (decode + convert) e lasciare la promozione GPU (`GpuAssetCache::acquire`) sotto il lock di execution, oppure in una sezione GPU condivisa che non crea un secondo renderer.
- Passare al dispatcher un contesto già preparato; il dispatcher non deve rileggere il piano.
- In caso di errore di prepare, rilasciare lo slot e rispondere con lo stesso stato `Failed` di oggi.

**Gate B:** nessun renderer/encoder/surface creato prima dell'execution lock; VRAM di picco invariata; output identico su 3 run; nessun deadlock con `LanesPerDaemon=2`; `asset_decode_ms` del job N+1 si sovrappone al render del job N (marker di timeline, non solo somma).

### Fase C — Pipeline e admission

- Aggiornare la policy di concorrenza: il prepare CPU può essere `ParallelDisjoint` rispetto a un execution in corso; l'execution resta seriale.
- Misurare il throughput su 20 item e su 100 item con lo stesso payload; riportare p50/p95 di `job_wall_ms` e `item/min`.
- Se il prepare CPU non è dominante o la GPU promotion richiede comunque il lock, chiudere la fase C con la misura.

**Gate C:** `job_wall_ms` p50 ridotto in modo misurabile sul corpus; nessun doppio render; nessun aumento VRAM oltre il budget; output byte-identico.

---

## 3. Rischi e vincoli

- **VRAM:** il prepare CPU non deve allocare surface GPU. La promozione va contata e limitata.
- **Thread safety:** `ImageCache` è già sharded e il resolver è esplicito; `GpuAssetCache` no. Non condividere un contesto GPU tra prepare ed execution senza una prova.
- **Determinismo:** l'ordine di decode non deve cambiare l'output. Il gate è MP4 byte-identico o hash frame identico.
- **Memoria:** un prepare in più tiene un piano decodificato e immagini CPU; il limite deve essere esplicito, non illimitato.
- **Fallback:** se il prepare anticipato fallisce, il job non deve restare appeso né trattenere una reservation.

---

## 4. Gate complessivo

- 3 run sullo stesso payload, con producer concorrente e `LanesPerDaemon=2`.
- `job_wall_ms` p50 e `item/min` misurati; nessuna stima percentuale prima del run.
- MP4 byte-identico (SHA-256) al controllo.
- VRAM di picco ≤ baseline + margine dichiarato.
- Nessun deadlock, nessun doppio render, nessuna reservation trattenuta su errore.
- `[NOT RUN al 2026-11-20]` separazione prepare/execution, producer
  concorrente, benchmark 20/100 item e parità end-to-end. La voce sullo split
  è superseded dall'implementazione e dalla prova IPC registrate sotto.
- `[CODICE]` corretto l'anchor di `daemon_admission_wait_ms`: prima partiva dopo
  l'acquisizione del lock seriale e quindi escludeva proprio l'attesa di lane;
  il valore ora include lock execution + reservation device. `[NOT RUN]` producer
  concorrente per dimostrare l'overlap.
- `[CODICE]` il daemon esegue `prepare_render_plan` sotto un singolo slot CPU
  prima di costruire `RenderJobAdmissionContext`, poi passa il relativo
  `PreparedRenderPlanContext` al dispatcher. Il plan non viene ricompilato
  durante l'execution. `job_started_at` include questa preparazione; admission
  wait resta misurato separatamente.
- `[MISURA]` build Release `-j10`, test CLI e test daemon render concurrency
  passano dopo la modifica. Il test
  `daemon socket survives a rejected render job and accepts the next one`
  blocca il primo dispatcher, accoda un secondo job e verifica che
  `PreparedRenderPlanContext::prepared_at` del secondo preceda il rilascio
  del lock del primo.
- `[CODICE]` il callback opzionale del gruppo render riceve solo un renderer
  già esistente. Prima dell'admission materializza il manifest, verifica che
  le chiavi ImageCache del resolver del piano e del runtime coincidano, deduplica
  le immagini per chiave e le decodifica in TBB nella cache CPU già posseduta
  dal runtime. Non crea renderer, encoder o surface e non promuove asset GPU;
  root diverse disabilitano il prefetch senza cambiare il percorso normale.
- `[MISURA]` il caso IPC
  `daemon socket survives a rejected render job and accepts the next one`
  usa un piano con immagine reale e root condivisa; blocca il dispatcher N,
  accoda N+1 e verifica sia `prepared_at` prima del rilascio del lock sia il
  timing positivo di prefetch nel suo contesto preparato. Il test verifica
  inoltre almeno due invocazioni con renderer caldo. Test mirato e suite CLI
  completa PASS. Metodo e scope sono in
  [`prepare_overlap_prefetch_20261121`](../../../ops/jobs/prepare_overlap_prefetch_20261121/REPORT.md).
- `[NOT RUN]` il marker del test prova la sovrapposizione ma non è una misura
  di throughput end-to-end. Restano il producer concorrente reale, i run su
  payload r2/r3 per 20/100 item, p50/p95/item-min, VRAM e parità output; fonti,
  promozione GPU e setup encoder restano sotto admission.
- `[MISURA]` 2026-11-21: A/B producer concorrente 20 item × 3 run sullo stesso
  fixture, con prefetch on/off. Software: mediana 12,29 vs 31,52 item/min;
  Vulkan render: 12,69 vs 24,69 item/min. Tutti gli output accoppiati sono
  byte-identici. Nel run Vulkan i sidecar indicano encoder software e 0 frame
  NVENC, quindi non è una misura encoder. Il prefetch non supera il gate di
  throughput e resta opt-in; i numeri e il perimetro sono nel report citato.
- `[MISURA — CORPUS MAPPA RASTER]` A/B aggiuntivo 2026-11-21: tre PNG
  approvati 1920×1080 come layer immagine, 20 item × 3 run e due producer IPC.
  Throughput p50: software 42,06/min con prefetch contro 54,71 senza (−23,1%);
  Vulkan 40,25 contro 41,49/min (−3,0%). `ImageCache` registra 165/180 hit
  software e 162/180 Vulkan con prefetch, 0/180 senza; i 120 MP4 per ciascun
  backend sono hash-identici fra varianti e run. Il prefetch riempie la cache
  ma non migliora il throughput netto. È un carico raster Chronon3D, non un
  replay del worker né un piano semantico `MAP`; il gate produttivo resta
  aperto. Artefatti e SHA delle 12 mappe approvate sono in
  `ops/jobs/throughput_map_assets_20261121/`; misure e sidecar nel report
  overlap collegato sopra.
- `[MISURA — MIX RENDERER CON MAPPA SEMANTICA]` Benchmark locale Chronon3D
  Vulkan/NVENC su 100 job per run × 3 run, 90 piani immagine e 10 piani
  ModernMap1: 141,88 item/min mediana con riuso CQP (99/100 adozioni) contro
  123,52 senza. È un mix renderer con mappe, non un replay del worker; il
  confronto reuse on/off produce 115/2.400 frame mappa non identici (SSIM
  medio mappa 0,999996). Pertanto il throughput locale supera 100/min, ma
  equivalenza end-to-end e gate worker non sono certificati. Il prefetch
  provato rimane spento perché il suo A/B dedicato è negativo. Dettagli e
  artefatti: [`throughput_map_assets_20261121`](../../../ops/jobs/throughput_map_assets_20261121/REPORT.md).

---

## 5. Demolition Debt

| Campo | Valore |
|---|---|
| **Owner** | maintainer daemon job runtime. |
| **Reason** | La separazione prepare/execution introduce un contesto preparato in più; va rimosso se la fase C non passa il gate. |
| **Exit condition** | Il prepare CPU è fuori dal lock e il gate di throughput passa; oppure la fase C è chiusa con misura e il contesto preparato viene rimosso. |
| **Equivalence test** | MP4 byte-identico e stessi stati job su 3 run. |
| **Removal scope** | Slot di prepare, campi sidecar e ramo di dispatcher introdotti solo per l'overlap. |
| **Status** | `ACTIVE` durante la migrazione. |

Nessuna nuova cache: il prepared-plan LRU resta in [`TICKET-DAEMON-JOB-RUNTIME`](TICKET-DAEMON-JOB-RUNTIME.md); questo ticket non lo duplica.
