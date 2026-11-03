# Ammissione GPU — due job batch indipendenti (2026-11-02)

Ambito: punto 3 dell'audit architetturale (concorrenza GPU) e gate di ammissione
di `Chronon3d/apps/chronon3d_cli/daemon/daemon_render_concurrency.hpp`. Ogni
numero qui sotto è **misurato** su questo host; niente è stimato. I report grezzi
sono in `docs/evidence/gpu-admission-two-jobs-2026-11-02/`.

## 1. Host e forma misurata

| Campo | Valore |
|---|---|
| device | NVIDIA RTX A4000, 16 376 MiB, driver 595.99.02 |
| occupazione iniziale | ~6.3 GiB residenti da altri processi (un `llama-server`, 3 daemon idle) ⇒ ~9.7 GiB liberi |
| backend / encoder | vulkan / NVENC `h264_nvenc`, preset `p1` |
| piano | `Chronon3d/tools/fixtures/vram-calibration-1080p-overlay.plan.json` |
| forma | 1920×1080 @ 24 fps, 48 frame, layer `color` + 2 `text` ⇒ lane FullGraph |
| lane effettiva | `execution plan resolved (reason: direct_yuv_capability_rejected); composite=2` |
| artefatti | 6 837 byte, identici fra le run della stessa forma |

## 2. Percorso ACCETTATO — due job indipendenti su due runtime

Due daemon propri (processi e pool distinti), un job ciascuno, barriera di
partenza comune. Il bench scalda prima i due pool, quindi misura su runtime caldi
come in produzione.

| Run | seriale (job 1 + job 2) | concorrente (wall) | speedup | risparmio |
|---|---|---|---|---|
| 1 | 713 + 714 ms = 1 427 ms | 975 ms | **1,99×** | 452 ms |
| 2 | 1 434 ms | 933 ms | **1,98×** | 501 ms |
| 3 (binario corretto) | 1 476 ms | 1 040 ms | **1,99×** | 436 ms |

VRAM (campionamento `nvidia-smi` a 50 ms, delta device picco − baseline):

| Misura | Delta device |
|---|---|
| due job concorrenti, run 2 | 1 528 MiB ⇒ ~764 MiB per job |
| due job concorrenti, run 3 | 1 527 MiB ⇒ ~764 MiB per job |
| harness `vram-probe -mode two-runtime-pool` (include lo startup dei due daemon) | 1 476 MiB, di cui 1 426 MiB attribuiti a `chronon3d_cli` |
| un solo job, CLI fredda, in sequenza | picco 943 MiB |

Conclusione del percorso accettato: due job FullGraph 1080p indipendenti
**convivono** e si sovrappongono quasi linearmente, a ~0.75 GiB per job. Il
deployment live usa già questa via: `/etc/renderinggen/renderinggen.yaml` dichiara
`socket_paths` con tre socket e le unit `chronon3d`, `chronon3d-b`, `chronon3d-c`
sono attive, quindi le tre `gpu_lanes` atterrano su tre runtime distinti.

## 3. Percorso RESPINTO — due job nello stesso daemon

Due `RENDER_JOB` sullo **stesso** daemon, con range esplicito e hint
`parallel_disjoint` (la classe che l'ammissione considera ammissibile):

| Caso | Esito osservato |
|---|---|
| stesso piano, chunk disgiunti 0–23 e 24–47 | entrambi ammessi: i due encoder NVENC si aprono a 41 ms di distanza, poi **nessun progresso per 75 s**; run annullata |
| piani diversi (`job_id` diverso), range esplicito | entrambi ammessi, il **processo daemon esce**; i client ricevono `ipc read reply: EOF` |

Il campo `speedup: 2` del report "prima del fix" è un artefatto del fallimento
(entrambi i job muoiono a ~700 ms), non un guadagno, e non va citato.

### Causa

`resolve_canonical_execution_parameters` deriva la lane dai requisiti della
richiesta: `direct_yuv = !pipe_composition && !requirements.composition_required`.
Una richiesta che non dichiara `composition_required` — come quella del chunk
producer — viene quindi **chargiata come DirectYUV** (1 GiB) anche quando il piano
è composito e verrà eseguito su FullGraph (misurato: `composite=2`). Con
`job_peak = 1 GiB` su 16 GiB la policy calcola 16 footprint e risponde
`ParallelDisjoint`: due FullGraph entrano nel dominio condiviso, che per questa
lane è **un solo renderer e un solo anello di superfici transitorie per device**.
Da qui lo stallo (stesso piano) e la morte del processo (piani diversi).

La calibrazione da 8,8 GB non è la causa di questo esito: è il floor della lane
FullGraph *video*, e in questo percorso la decisione era già stata presa su 1 GiB.

## 4. Verdetto e fix

| Voce | Esito |
|---|---|
| concorrenza multi-runtime | **ACCETTATA**, 1,98×, ~0,75 GiB/job |
| concorrenza dentro un daemon | **RESPINTA**: non certificabile, il daemon va in stallo o esce |
| abbassamento del floor 8,8 GB | **non eseguito**: non è il collo di bottiglia di questa lane |
| `admitted_slots ≥ 2` | **osservato** nell'unico caso in cui il daemon lo concede; l'esito è il fallimento sopra, quindi il criterio si chiude come rifiuto motivato, non come pass |

Fix applicato — la forma del piano viene letta **prima** dell'ammissione
(`extract_plan_admission_shape`) e un piano che richiede compositing non può
entrare nel dominio condiviso, qualunque hint dichiari il producer:

* `PlanAdmissionShape.requires_composition` è `true` per qualunque piano che non
  sia esattamente **un** layer `video` senza chiavi di compositing, e resta `true`
  anche se il piano è illeggibile (fail closed);
* `daemon_service_ipc_render_job.cpp` calcola
  `parallel_disjoint = resolved.parallel_disjoint && !requires_composition` e
  logga il rifiuto.

Verifica del fix sullo stesso caso che prima uccideva il daemon (piani diversi,
range esplicito, `parallel_disjoint`):

* entrambi i job **completano** (6 837 byte ciascuno);
* wall 769 ms e 1 495 ms ⇒ il secondo attende il lock, nessuna sovrapposizione;
* il log contiene due `parallel_disjoint refused ... requires composition`;
* `chronon3d_daemon_render_concurrency_tests`: 31 casi, 177 asserzioni, **0
  fallimenti** (include i 6 casi nuovi sulla forma del piano).

## 5. Riproduzione

```bash
# Percorso accettato: due job su due runtime caldi (src: RenderingGen/renderinggen)
go run ./cmd/two-job-warm-bench \
  -plan ../Chronon3d/tools/fixtures/vram-calibration-1080p-overlay.plan.json \
  -chronon-bin ../Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli \
  -assets-root ../Chronon3d -output-dir /tmp/warm -frames 48

# Percorso respinto: due RENDER_JOB sullo stesso daemon (strumento di laboratorio)
go run ./cmd/chunk-overlap-bench \
  -plan ../Chronon3d/tools/fixtures/vram-calibration-1080p-overlay.plan.json \
  -plan2 /tmp/plan-b.json \
  -chronon-bin ../Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli \
  -assets-root ../Chronon3d -output-dir /tmp/chunks -jobs 2 -same-range

# Baseline VRAM a due runtime (harness esistente)
./renderinggen/bin/vram-probe -mode two-runtime-pool \
  -plan ../Chronon3d/tools/fixtures/vram-calibration-1080p-overlay.plan.json \
  -chronon-bin ../Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli \
  -assets-root ../Chronon3d -output-dir /tmp/vram -samples /tmp/vram.log \
  -out /tmp/vram/report.json -daemons 2 -daemon-lanes 1 -pool-interval 20ms -hold 5s
```

## 6. Limiti dell'evidenza

* Una forma (1080p overlay FullGraph) su un host, con altri processi residenti sul
  device; un'altra shape, o un altro host, ri-decide dai propri numeri.
* La concorrenza in un daemon **non** è dichiarata sicura: è stata provata e ha
  fallito, quindi resta seriale per costruzione.
* Lo shape video FullGraph (decodifica + compositing) non è stato misurato qui:
  resta sul floor 8,8 GB e `vram-calibration-1080p-video.plan.json` non rende sul
  percorso daemon (fallisce con `exit code 1`), quindi nessun numero lo copre.
