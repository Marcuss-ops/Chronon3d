# VRAM a due runtime — harness di laboratorio (2026-09-16)

Ambito: `TICKET-DAEMON-JOB-LIFECYCLE` §3/§5. Host: RTX A4000 16 GB (driver
595.91.07), Vulkan + NVENC, lane `linux-video-release`. Ogni numero qui sotto è
**misurato**; niente è stimato.

## 1. Perché un secondo strumento

Il probe singolo (`cmd/vram-probe -mode single`, `internal/vramprobe/probe.go`)
sottomette i job attraverso il **percorso queue normale**, cioè attraverso il
daemon che serializza `RENDER_JOB` dietro `m_render_job_mutex`. Finché quel mutex
è preso, esiste **un solo job residente ed eseguito**: quel delta è il costo
incrementale di *un* job attivo e non può dimostrare che due convivano.

La misura separata del 2026-09-16 (evidenza
`RenderingGen/multilingual_overlays_v1/evidence/vram_two_runtime_2026-09-16.json`)
usava due processi **CLI cold-start**: non è lo stesso oggetto di due **runtime
caldi con pool residenti**, ed è per questo che il ticket non l'ha promossa ad
autorizzazione.

Il nuovo harness (`cmd/vram-probe -mode two-runtime-pool`,
`internal/vramprobe/pool.go`) parte **due daemon propri** (due processi, due
runtime, due pool caldi) su socket distinti, rende **un job ciascuno in
concorrenza reale** (barriera di partenza, non ciclo sequenziale) e campiona il
device in **cinque fasi etichettate**: `idle` → `warm_pools` → `rendering` →
`resident_hold` (pool vivi, nulla in render) → `after_shutdown`.

Due proprietà rendono la misura non auto-ingannevole:

* se un daemon muore durante lo startup l'harness **fallisce**, non misura
  (e il pool non può più bloccarsi: il canale one-shot `cmd.Wait` è stato
  sostituito da un esito memorizzato, regressione in `daemon_pool_test.go`);
* se i socket non sono **posseduti** dall'harness (`owned_daemons != daemons`) la
  misura è rifiutata: rendere attraverso un daemon altrui non è "due runtime".

## 2. Comando riproducibile

```bash
cd RenderingGen
renderinggen/bin/vram-probe -mode two-runtime-pool \
  -plan phrase_animations_v1/01_discipline_gives_power_a_direction__kinetic_split_word_render_plan.json \
  -chronon-bin "$CHRONON_RELEASE/apps/chronon3d_cli/chronon3d_cli" \
  -assets-root testdata/golden -output-dir /tmp/vram-two-runtime \
  -samples multilingual_overlays_v1/evidence/vram_two_runtime_pool_samples_2026-09-16.log \
  -out     multilingual_overlays_v1/evidence/vram_two_runtime_pool_2026-09-16.json \
  -daemons 2 -daemon-lanes 1 -interval 20ms -hold 12s -timeout 4m
```

`-interval 20ms` è deliberatamente più fine dei 50 ms del primo probe: il segnale
interessante è il picco breve durante l'admission.

## 3. Metadata della shape misurata (dal piano, non dichiarata a mano)

| Campo | Valore |
|---|---|
| plan | `01_discipline_gives_power_a_direction__kinetic_split_word_render_plan.json` |
| plan SHA-256 | `b14ec958a2e951348a4089a6186f85cd3c4d1449c88e4f53ba29f94e54b677b7` |
| canvas | 1920×1080 @ 24/1 fps, 120 frame, h264/mp4 |
| layer types | `color`, `text` (overlay senza sorgente video) |
| backend / encoder | vulkan / nvenc, preset `p1` |
| device | NVIDIA RTX A4000, driver 595.91.07, 16 376 MiB |
| runtimes | 2 daemon posseduti, 1 lane ciascuno, socket privati (`mktemp`) |
| campionamento / hold | 20 ms / 12 s |
| campioni | vedi `samples` nel report (≈270) |

## 4. Esito (report canonico `vram_two_runtime_pool_2026-09-16.json`)

| Fase | device `memory.used` | attribuito a `chronon3d_cli` |
|---|---|---|
| idle (1 daemon di sistema già residente) | 332 MiB | 248 MiB |
| warm pools (2 runtime in più, fermi) | 334 MiB | 248 MiB |
| rendering (2 job FullGraph 1080p in volo) | **1 945 MiB** | **1 814 MiB** |
| resident hold (pool vivi, nulla in render) | 768 MiB | ~644 MiB |
| after shutdown (pool chiusi) | 282 MiB | 232 MiB |

* delta rendering vs idle: **1 613 MiB** per **due** render concorrenti;
* i due artifact sono **byte-identici** (`sha256 02c25c77d965…`, 238 649 byte):
  stessa shape, stesso host, stesso encoder ⇒ il numero è confrontabile con un
  render singolo, non è un workload diverso;
* `second_runtime_resident_mib ≈ 1` con `warm_pools_touched_device: false`: un
  pool caldo che non ha ancora ammesso un job **non ha toccato il device**
  (allocazione lazy). Il costo residente va letto nella fase `resident_hold`
  (≈+436 MiB per due pool dopo il primo render), non nella fase `warm_pools`.
  Questo corregge l'ipotesi implicita "pool caldo = VRAM già spesa".

### 4.1 Ripetibilità e il limite del contatore device-wide

Tre esecuzioni consecutive, stesso comando, stesso host, minuti di distanza:

| run | device idle → rendering | attribuito a `chronon3d_cli` | job (ms) |
|---|---|---|---|
| A (18:54) | 499 → **5 780** MiB | 248 → 1 700 | 3 401 / 3 301 |
| B (18:56) | 342 → 1 815 MiB | 248 → 1 692 | 2 668 / 2 528 |
| C (18:58) | 332 → 1 945 MiB | 248 → 1 814 | 2 836 / 2 839 |

La **attribuzione per processo** è stabile (1 692–1 814 MiB per due render in
volo, spread ~7%); il **contatore device-wide** nella stessa finestra varia di
oltre 3× (1 473 → 5 281 MiB di delta). La causa è l'host condiviso: `memory.used`
è device-wide e include consumatori esterni all'harness (nel run A ≈4 GB non
attribuiti ad alcun processo `chronon3d_cli`, presenti anche in idle a 3 527 MiB
prima della sessione). Conseguenza per la calibrazione: **il numero da usare non
è `memory.used`, è l'attribuzione per processo** (o, meglio, la telemetria interna
degli allocator, come chiede l'exit condition del ticket). Il report porta
deliberatamente entrambe le serie e il raw `samples` per ri-derivare qualunque
statistica.

## 4.2 La coda di vita del job su questo percorso (misurata, stessa run)

L'harness parla ai daemon via IPC, quindi la stessa run misura anche la coda di
vita **fuori** dalla serializzazione di produzione. Due render concorrenti,
120 frame, 1920×1080:

| voce | job 0 | job 1 |
|---|---|---|
| `render_ms` | 405.0 | 413.7 |
| `encode_ms` | 99.5 | 89.3 |
| `writer_encode_ms` | 243.1 | 232.0 |
| `wall_ms` | 2 335.5 | 2 351.2 |
| `execute_ms` | 2 551.6 | 2 562.5 |
| `session_teardown_ms` | **173.0** | **175.9** |
| ↳ `encoder_reset_ms` | 45.6 | 43.5 |
| ↳ `full_graph_reset_ms` | 127.1 | 132.2 |
| ↳ `sidecar_write_ms` | 11.0 | 11.3 |
| `slot_pool_close_ms` / `surface_release_ms` | 0.0 / 0.0 | 0.0 / 0.0 |
| `queue_close_ms` / `writer_join_ms` / `decoder_reset_ms` | 0.0 | 0.0 |

Due conseguenze per l'item aperto "~1 s di teardown intermittente":

1. su questa shape e su questo percorso la coda è **interamente attribuita** e
   vale ~174 ms (ogni voce sopra ha un timer; nessun residuo non nominato);
2. quindi il ~1 s intermittente **non** sta nel teardown del daemon su questa
   shape: va cercato nel percorso serializzato di produzione (o nella coda del
   **worker**), non reintroducendo timer già presenti. `encoder_reset` e
   `full_graph_reset` sono i due candidati che restano da misurare là.

## 5. Cosa questo numero autorizza — e cosa no

Autorizza:

* **falsifica** la calibrazione globale ~8.8 GB per job 1080p FullGraph su questa
  shape: due render concorrenti stanno in ~1.6–1.8 GB attribuiti;
* fornisce il costo residente incrementale di un runtime caldo (fase
  `resident_hold`) e il costo in volo di un render overlay 1080p/120f.

**Non** autorizza a rimuovere `m_render_job_mutex`. Il report lo dichiara nel
documento stesso (`authorizes_mutex_removal: false`) perché:

1. è **una** shape su **un** host, mentre `derive_vram_working_set` va alimentato
   da una suite su 3–5 shape certificate (overlay-only, video-sorgente FullGraph,
   DirectYUV, 4K);
2. il percorso **daemon** carica VRAM per plan all'admission: due pool caldi e
   fermi non sono due pool che ammettono contemporaneamente su shape diverse;
3. serve la ripetizione con attribuzione interna degli allocator, perché il
   contatore device-wide su un host condiviso non è una misura (vedi §4.1).

## 6. Artefatti

* harness: `RenderingGen/renderinggen/internal/vramprobe/pool.go`
  (+ `cmd/vram-probe -mode two-runtime-pool`), regressione del deadlock di
  startup in `RenderingGen/renderinggen/internal/chronon/daemon_pool_test.go`;
* report canonico: `RenderingGen/multilingual_overlays_v1/evidence/vram_two_runtime_pool_2026-09-16.json`;
* serie grezza: `.../evidence/vram_two_runtime_pool_samples_2026-09-16.log`
  (`phase elapsed_s device_used_mib chronon3d_cli_used_mib gpu_util_pct`);
* misura CLI cold-start precedente (non sostituita, ma distinta):
  `.../evidence/vram_two_runtime_2026-09-16.json`.
