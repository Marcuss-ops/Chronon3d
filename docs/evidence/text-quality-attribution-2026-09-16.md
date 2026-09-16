# Attribuzione qualità testo — 2026-09-16

Evidenza del diagnostico P0-0 di [`docs/tickets/TICKET-TEXT-QUALITY-V2.md`](../tickets/TICKET-TEXT-QUALITY-V2.md).

Obiettivo: stabilire **quale leg** causa la perdita di qualità percepita sul testo,
prima di toccare qualunque riga. Host: `YOutube`, RTX A4000 16 GB, driver NVIDIA,
lane `build/chronon/linux-video-release`.

Materiale: workspace di un job **reale** conservato (`pipeline.keep_workspace`)
— plan `chronon.render-plan.v2`, 1920×1080@24, 1440 frame, 33 layer, 31 cue di
testo, font `Montserrat-Bold`, stile cue: `fill #FFFFFF`, `stroke #000000` w3,
`shadow #000000` 0.95 blur 6 offset [2,3].

---

## Leg A — quale lane usano davvero i job di produzione

**PASS (misurato).** Ultimi 30 minuti di traffico reale:

```
24 execution plan resolved (reason: required_gpu_native_full_graph)
 0 execution plan resolved (reason: direct_yuv_*)
```

E su un plan reale la richiesta esplicita di DirectYUV è **rifiutata per
classificazione**, quindi non è una lane che si possa forzare:

```
[video] DirectYUV composition classification rejected the candidate: requires_full_frame_rgba (layer 'source')
[video] FAIL_CLOSED: requested execution cannot run on this host (resolver reason: direct_yuv_capability_required)
```

Conseguenza: i difetti specifici di DirectYUV (fill binario
`fill_alpha = signed_distance > 0.5 ? 1 : 0`, ombra binomiale 3×3, blend alpha
contro background *encoded*) sono **latenti** su questo host, non la causa di
ciò che si vede negli artifact pubblicati (che sono FullGraph).

## Leg B — quanto pesa l'encode

**PASS (misurato).** Stesso frame, stesso plan, stessa lane, due soli parametri
di encoder diversi. Plan a sfondo piatto (testo bianco su grigio 0.5) per non
confondere il bordo con lo sfondo video:

| Artifact | Impostazioni | Bitrate | PSNR vs prod |
|---|---|---|---|
| `flat_prod.mp4` | `rc=driver-default, preset=p1` (produzione) | 84 kbps | — |
| `flat_hq.mp4` | `rc=constqp, qp=18, preset=p5` | 65 kbps | **53.30 dB** |

`mean|d| = 0.034/255`; sui pixel di testo `2.23/255`; **16 pixel su 2 073 600**
oltre 64/255 (0.0008% del frame). Su un clip con video reale (frame 60 dello
stesso plan) le due codifiche distano `41.05 dB`, `mean|d| = 1.30/255`.

**Verdetto: su contenuto overlay l'encode non è la causa.** Il default di
produzione (`driver-default`) non è un preset economico mascherato: a parità di
contenuto statico produce un file *più grande* di `qp18/p5`. Va detto però che
`driver-default` significa "nessun rate control richiesto": è il driver a
scegliere, e questo resta un contratto non dichiarato.

Contratto verificato: `--rate-control crf` è **rifiutato** su NVENC in questa
release —

```
Invalid encoder configuration: rate control 'crf' (constant quality) is not
supported on the NVENC backend in this release; use qp, bitrate/vbr or cbr, or
omit rate control to keep the NVENC driver default (constant-quality NVENC needs
a certified profile)
```

quindi una leg "HQ" si costruisce con `qp` o `bitrate`, non con `crf`.

## Leg C — la verità pre-encode (PNG) NON è disponibile

**FAIL (misurato).** `render --start-frame N --end-frame N -o x.png` scrive un
frame, ma **non contiene il testo**:

| File | min / p50 / p99 / max (luma) | livelli distinti | pixel bianchi (>0.72) |
|---|---|---|---|
| `flat_still.png` (still) | 0.498 / 0.498 / 0.498 / 0.498 | **1** | 0 |
| `flat_prod_f0.png` (mp4) | 0.000 / 0.502 / 0.502 / 1.000 | 176 | 9 550 |

È un frame **grigio uniforme**: la via still non compone i layer di testo. Su un
plan con sorgente video la differenza è ancora più netta (`mean|d| = 177/255`,
PSNR 2.64 dB).

Conseguenza: **il golden pre-encode oggi non è producibile**, quindi una
certificazione pixel-level della qualità testo non è possibile — coerente con il
canary `chronon3d_text_tile_raster_canary_tests` ancora rosso
(`docs/FOLLOWUP_TICKETS.md:16`). Questo è il primo lavoro infrastrutturale del
ticket, non un dettaglio.

## Leg D — VRAM per shape (gate della serializzazione)

**PASS (misurato due volte, indipendenti).** Campionamento `nvidia-smi` a 50 ms:

| | device idle | device peak | daemon idle | daemon peak | delta |
|---|---|---|---|---|---|
| run 1 (`vram-probe-1`) | 3 525 MiB | 4 117 MiB | 226 MiB | 801 MiB | **592 MiB** |
| run 2 (`flat-bg-1`) | 3 517 MiB | 4 110 MiB | 218 MiB | 793 MiB | **593 MiB** |

Job overlay FullGraph (121 frame, `vulkan_frames=121`, `gpu_native_encode_frames=121`,
`rgba_to_nv12_frames=121`, `gpu_lane_wait ≈ 0.01 ms`).

La calibrazione citata da `apps/chronon3d_cli/daemon/daemon_render_concurrency.hpp:29-35`
(~8.8 GB peak per 1080p FullGraph) **non riproduce su questa shape**.
Limite dichiarato: con il mutex c'è **un solo** job GPU residente alla volta,
quindi 592 MiB è il costo incrementale di *un* job attivo — **non** prova che due
convivano. Serve l'harness a due runtime simultanei (§Prossimi passi).

## Leg E — budget serializzato del daemon (contesto throughput)

Dal log del daemon con delta di timestamp, job senza contesa:

| Fase | Costo per overlay |
|---|---|
| render loop 121 frame | 230–258 ms (~500 fps) |
| pool-warm canvas + pre-import di 6 execution slot | per job (101 boot per 100 job) |
| gap tra scrittura sidecar e `receipt:` | **1 280 ms** (identico su job da 937 ms e da 1 980 ms) |
| receipt | 284 ms daemon (di cui probe 282) + 253 ms ri-verifica worker |
| handoff al job successivo | 62 ms |

## Leg F — due runtime FullGraph simultanei (harness, 2026-09-16)

**PASS.** Due `chronon3d_cli render` in parallelo (due processi, due runtime
indipendenti, stesso plan/prepared/assets, `require_gpu_native`, 120 frame),
campionamento `nvidia-smi` a 50 ms (84 campioni):

| | MiB |
|---|---|
| device idle | 3 517 |
| **peak con 2 exporter concorrenti** | **6 319** |
| delta per 2 job concorrenti | 2 802 |
| device totale | 16 376 |

Entrambi i render sono terminati `rc=0` e si sono **davvero sovrapposti**: `render_ms`
1 287 ms e 1 201 ms contro 1 071 ms da solo. Gli artifact dei due processi sono
**bit-identici** (`md5 0f9f51e9…`, lo stesso del run singolo) → la concorrenza non
introduce nondeterminismo su questo shape.

Limite dichiarato: sono due processi *cold-start*, mentre il daemon mantiene pool
caldi tra i job e la sua admission carica la VRAM per plan. Quindi questo numero
**falsifica** la tesi "due exporter 1080p FullGraph non entrano in 16 GB" (la
calibrazione 8.8 GB/job ne predirebbe 17.6 GB), ma **non autorizza** da solo la
rimozione di `m_render_job_mutex`: quella resta legata a una calibrazione
strumentata per shape che alimenti `derive_vram_working_set`.

## Attribuzione aperta

Non attribuito da nessun timer: il gap da 1 280 ms. Nel percorso **single-shot**
della CLI lo stesso tratto vale ~250 ms, quindi non è intrinseco all'export.
Inoltre `CHRONON_RENDER_JOB_SLOTS=2` è presente nell'ambiente del daemon e
**non fa nulla** (constexpr `kRenderJobExecutionSerialized`): l'intento
configurato è 2, la capacità effettiva 1.

Nota di incidente (registrata, non diagnosticata): durante un `strace` sul
daemon questo è terminato con
`terminate called after throwing an instance of 'std::system_error' what(): Invalid argument`
ed è stato riavviato da systemd. Il PID è cambiato (1527 → 1036443).

---

## Come riprodurre

```bash
BIN=build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli
# workspace di un job reale: pipeline.keep_workspace=1 (o RENDERINGGEN_PIPELINE_KEEP_WORKSPACE=1)
W=/tmp/renderinggen/jobs/<job>; cp -r $W /tmp/qdiag/ws

C="$BIN render --plan /tmp/qdiag/ws/plan.json --prepared-package /tmp/qdiag/ws/prepared.json \
     --assets-root /tmp/qdiag/ws --backend vulkan --gpu-hot-path-mode require_gpu_native --hardware nvenc"

$C --encode-preset p1 --start-frame 0 --end-frame 119 -o /tmp/qdiag/out.mp4         # produzione
$C --encode-preset p5 --rate-control qp --qp 18 --start-frame 0 --end-frame 119 -o /tmp/qdiag/hq.mp4
$C --start-frame 60 --end-frame 60 -o /tmp/qdiag/still.png                          # still (oggi senza testo)
$C --gpu-hot-path-mode require_direct_yuv --start-frame 0 --end-frame 119 -o /tmp/qdiag/dyuv.mp4  # FAIL_CLOSED

# leg VRAM: due job overlay via coda + campionamento nvidia-smi a 50 ms
python3 renderinggen-vram-probe.py --source-manifest manifest_run2.json --batch-id vram-probe-N

# metriche a pixel
python3 tools/quality_probe/quality_probe.py --ref cur_f60.png --test hq_f60.png --region 0,378,1920,1080
```

## Esito sintetico

| Leg | Esito | Cosa dice |
|---|---|---|
| A lane | PASS | 24/24 FullGraph: i fix DirectYUV non spiegano gli artifact pubblicati |
| B encode | PASS | 53.30 dB fra produzione e HQ su overlay ⇒ encode **non** prima causa |
| C verità pre-encode | FAIL | la via still non renderizza il testo ⇒ niente certificazione pixel-level |
| D VRAM shape overlay | PASS | 592/593 MiB per job attivo, riproducibile ⇒ la calibrazione 8.8 GB non copre questa shape |
| E budget daemon | PASS (parziale) | 1 280 ms non attribuiti per job + doppia verifica dell'artifact + slot=2 no-op |
