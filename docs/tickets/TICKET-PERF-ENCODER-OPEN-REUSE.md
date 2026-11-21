# TICKET-PERF-ENCODER-OPEN-REUSE — l'apertura NVENC per item è ancora un costo fisso

**Stato:** PARTIAL — aggiornato 2026-11-21. Riutilizzo NVENC CQP nativo misurato su 100 render job sintetici: 99/100 adozioni e +20,3% item/min; 7.200/7.200 frame decodificati pixel-identici. Sul mix locale di 90 piani immagine e 10 piani semantici ModernMap1, tre run raggiungono 141,88 item/min con riuso contro 123,52 senza (+14,9%); adozione 99/100. Il confronto pixel non passa: 115/2.400 frame mappa differiscono, anche se SSIM medio per i dieci job mappa è 0,999996. A/B sui piani caption r2/r3: p1/CQP QP23 +9,4% FPS rispetto a P4 driver-default, SSIM 0,9943 e file +15,8–16,3%. Il flag è già attivo nel daemon, ma P4/driver-default non è adottabile dal gate CQP. Restano il gate qualità/bitrate editoriale e revisione visiva, root cause/accettazione delle differenze mappa, e writer P4 segmentato.
**Area:** `apps/chronon3d_cli/utils/video/native_av_encoder_open_negotiate.cpp`, `apps/chronon3d_cli/utils/video/native_av_encoder_lifecycle.cpp`.
**Owner:** maintainer encoder video.
**Dipende da:** niente per la misura. L'adozione del riuso dipende dal gate qualità p1-vs-p4.
**Non sostituisce:** [`TICKET-PERF-100X-VERIFIED-PLAN`](TICKET-PERF-100X-VERIFIED-PLAN.md) e [`TICKET-MAX-THROUGHPUT-OVERLAY-ARCHITECTURE`](../../../refactored/docs/operations/TICKET-MAX-THROUGHPUT-OVERLAY-ARCHITECTURE.md). Quel documento descrive il riuso come leva; questo ticket lo chiude o lo dichiara inapplicabile con una misura.

---

## 0. Evidenza

`[MISURA]` `encoder_open_nvenc_ms` nei job del 2026-11-19:

| Job / item | `encoder_open_nvenc_ms` | `prepare_ms` | `job_wall_ms` |
|---|---:|---:|---:|
| entity-rect-caption-oswald r2 / 0 | 98,7 | 3 273 | 5 951 |
| entity-rect-caption-oswald r2 / 1 | 88,8 | 3 387 | 5 805 |
| entity-rect-caption-abril r2 / 0 | 38,9 | 3 538 | 7 861 |
| entity-rect-caption-abril r2 / 1 | 44,2 | 3 903 | 8 213 |
| entity-rect-caption-oswald r3 / 0 | 61,4 | 2 099 | 4 988 |
| entity-rect-caption-oswald r3 / 1 | 52,6 | 2 182 | 4 666 |
| entity-rect-caption-abril r3 / 0 | 79,0 | 2 172 | 5 074 |
| entity-rect-caption-abril r3 / 1 | 71,5 | 2 310 | 4 855 |

Fonti: gli stessi `*-full.json` del ticket asset, `chronon_telemetry.job.runtime.encoder_open_nvenc_ms`. Il costo è 39–99 ms/item; su un item da 300–500 ms è il 10–30%.

`[CODICE]` Il riuso cross-job è attivato dal daemon salvo override esplicito (`CHRONON_ENCODER_SESSION_REUSE=0`), ma resta limitato a configurazioni certificate:

- `apps/chronon3d_cli/daemon/daemon_service.cpp:246-260`: il daemon imposta `CHRONON_ENCODER_SESSION_REUSE=1` se l'ambiente non lo definisce; `0` è il kill switch.
- `apps/chronon3d_cli/utils/video/native_av_encoder_lifecycle.cpp:33-60`: l'adozione richiede il flag abilitato e la configurazione supportata.
- `apps/chronon3d_cli/utils/video/native_av_encoder_open_negotiate.cpp:560-590`: `codec_session_config_supported()` accetta solo `ConstantQp`; per NVENC `h264_nvenc`/`hevc_nvenc`, per software `libx264` zero-latency con `encode_threads==1`.
- `apps/chronon3d_cli/utils/video/native_av_encoder_open_negotiate.cpp:601-641`: `try_adopt_codec_session()` adotta solo una sessione parkata **unflushed**, con fingerprint identico.
- `[CODICE]` Il writer P4 segmentato non adotta una sessione tra job indipendenti: è dichiarato fuori dal meccanismo in `TICKET-MAX-THROUGHPUT-OVERLAY-ARCHITECTURE` §A1 e nel commento di `codec_session_reuse_enabled()`.

`[INFERITO]` Se la produzione usa P4 driver-default (o CRF), il flag ambientale è acceso ma la sessione non passa il gate CQP: ogni item paga l'apertura. Se la produzione può usare p1/CQP senza regressione oltre soglie di qualità e bitrate dichiarate, il riuso è già attivo nel daemon e l'adozione è misurabile; non serve un'ulteriore attivazione operativa. Le due decisioni (preset e writer P4) vanno tenute separate.

---

## 1. Obiettivo

Ridurre il costo di apertura encoder per item a ≤10 ms, **senza** cambiare la qualità percepita, scegliendo la configurazione che lo consente:

1. **Percorso A — p1/CQP + riuso:** se p1/CQP passa il gate qualità sul corpus overlay, renderlo la configurazione del job overlay e abilitare il riuso cross-job. Il riuso esiste già; il lavoro è la decisione qualità + l'attivazione operativa.
2. **Percorso B — P4 senza riuso:** se P4 è obbligatorio per qualità/bitrate, estendere il riuso al writer segmentato (o pre-aprire/warmare la sessione) e misurare il limite. Se non è sicuro, dichiarare 39–99 ms come floor e chiudere il ticket con l'evidenza.

---

## 2. Piano (split)

### Fase A — Baseline e A/B qualità

- Raccogliere 10 item overlay rappresentativi (testo, immagini, mappa, catene) e renderizzare con:
  - p1/CQP + `CHRONON_ENCODER_SESSION_REUSE=1`;
  - P4 segmentato attuale (controllo).
- Misurare: `encoder_open_nvenc_ms`, `job_wall_ms`, bitrate, dimensione file, SSIM/VMAF o confronto frame hash dove il codec è deterministico.
- Riportare la varianza su 3 run. Non usare una singola coppia come prova.

**Gate A:** la configurazione p1/CQP è accettabile solo se la qualità non regredisce oltre la soglia dichiarata e il bitrate resta nel budget. Se non lo è, passare al percorso B senza dichiarare un guadagno.

### Fase B — Certificazione dell'adozione nel traffico daemon

- Il daemon abilita già `CHRONON_ENCODER_SESSION_REUSE=1` per default; verificare l'ambiente effettivo e mantenere `0` come kill-switch. Il flag non trasforma P4/CRF in CQP e non forza l'adozione.
- Dopo il gate A, verificare l'adozione sui job p1/CQP reali senza cambiare il rate-control degli altri job.
- Verificare che il fingerprint copra preset, rate control, risoluzione, fps, colore, direct-YUV e input format; un mismatch deve fallire chiuso.
- Aggiungere al sidecar `codec_session_adopted` e `codec_session_park_reason`, così il riuso è osservabile e non solo loggato.
- Test: job consecutivi con stesso piano, piani diversi, config diversa, output troncato, daemon restart.

**Gate B:** `encoder_open_nvenc_ms` p50 ≤10 ms dal secondo item; output quality parity; nessun crash/daemon kill; nessun frame mancante; fingerprint mismatch → nuova sessione, non adozione.

### Fase C — P4 / fallback

- Se P4 resta obbligatorio: misurare il costo di apertura separando `open_nvenc`, `open_hw_ctx`, `mux_header`; valutare pre-open o pool di sessioni **solo** se il writer segmentato può certificare il boundary.
- Se non è certificabile, chiudere con la misura e lasciare P4 come floor documentato.

**Gate C:** per P4, o `encoder_open_nvenc_ms` ≤10 ms con parità qualità, oppure decisione esplicita `WONTFIX` con numeri e motivazione.

---

## 3. Rischi e vincoli

- **Qualità:** p1 vs p4 non è un dettaglio; il gate deve essere visivo e oggettivo, non solo velocità.
- **Determinismo:** il riuso CQP è già certificato per output byte-identico; non estenderlo a CRF/adattivo senza una prova.
- **Stato encoder:** una sessione parkata unflushed non può essere flushata; ogni adozione deve mantenere il contratto PTS/IDR.
- **Produzione:** l'env va attivato sulle unità giuste e verificato da `/proc/<pid>/environ`; non basta il default del codice.
- **Segmenti:** il writer P4 ha un lifecycle diverso; non confondere il riuso whole-timeline con il multi-segment.

---

## 4. Gate complessivo

- 3 run per configurazione, stesso payload e stesso host.
- `encoder_open_nvenc_ms` p50 ≤10 ms dal secondo item, oppure `WONTFIX` motivato.
- Qualità: soglia dichiarata prima del run; bitrate/dimensione entro budget.
- Output: MP4 byte-identico dove il codec lo consente; altrimenti SSIM/VMAF e revisione visiva.
- Nessun crash, nessun frame mancante, nessuna sessione adottata con fingerprint diverso.
- `[MISURA]` A/B preliminare p1/P4, QP 20, tre run per preset sul fixture
  software di due frame: SSIM abbinato 0,999911; file 3.394/3.396 byte;
  `encoder_open_nvenc_ms` p50 36,68/34,70 ms (prime aperture, senza riuso).
  Sidecar, MP4 e limiti in
  [`encoder_nvenc_p1_p4_20261120`](../../../ops/jobs/encoder_nvenc_p1_p4_20261120/REPORT.md).
- `[NOT RUN]` gate A/B, attivazione e benchmark sul corpus rappresentativo.
- `[PARTIAL]` `codec_session_adopted` aggiunto al sidecar e coperto dal test schema.
- `[NOT RUN]` `codec_session_park_reason`, A/B sul corpus e adozione dalla lane P4: non abilitare in produzione senza il gate qualità e il test del lifecycle segmentato.

### Verifica NVENC locale — 2026-11-20

- `[MISURA]` Build del target con CUDA interop e Vulkan abilitati:
  `cmake --build build/chronon/linux-video-test --target chronon3d_native_decoder_teardown_tests -j10` → PASS.
- `[MISURA]` Test espliciti con `CHRONON3D_RUN_NVENC_TESTS=1` e `--no-skip`:
  `Cross-job NVENC constant-QP session reuses with pixel parity` e
  `NativeAvEncoder: 5-job NVENC cross-item codec session reuse chain` → 2/2
  casi PASS, 309 asserzioni PASS, inclusa verifica di 24 frame.
- `[NOT RUN]` Il test certifica il lifecycle CQP locale, non il gate p1-vs-P4:
  mancano tre run per configurazione sul corpus rappresentativo, soglia di
  qualità/bitrate, p50 di apertura dal secondo item e adozione nel writer P4.

### Replay nativo NVENC CQP — 2026-11-21

- `[MISURA]` A/B sullo stesso piano animato da 24 frame, cache immagini calda,
  Vulkan + encoder `native` / `h264_nvenc`, CQP QP 23, preset p1, due producer
  concorrenti. Dataset da 10 job × 3 run: reuse 9/10 adozioni per run; throughput
  107,28 / 107,38 / 111,49 item/min contro 102,35 / 101,62 / 97,61 senza
  reuse. I 720/720 frame decodificati sono pixel-identici.
- `[MISURA]` Dataset da 100 job × 3 run: reuse 99/100 adozioni per run;
  throughput 179,98 / 173,40 / 184,83 item/min contro 152,00 / 147,23 /
  149,66 senza reuse. Mediana +20,3%; IPC wall p50 620 vs 737 ms. I 7.200/7.200
  frame decodificati sono pixel-identici e `ffprobe` riporta 24 frame per file.
- Per ogni sessione adottata `codec_session_adopted=true` e
  `encoder_open_nvenc_ms=null`: il codice salta l'apertura. Ogni run apre una
  sola sessione nuova a 32,65–36,11 ms. Il rate-control è CQP, quindi la
  parità non viene dedotta da un confronto p1/P4: il gate qualità/bitrate sul
  corpus editoriale rappresentativo e il percorso P4 restano da misurare.
- Report, input fixture, MP4, sidecar e log:
  [`prepare_overlap_prefetch_20261121`](../../../ops/jobs/prepare_overlap_prefetch_20261121/REPORT.md).

### A/B encoder su piani caption reali — 2026-11-21

- `[MISURA]` Quattro piani semantici r2/r3 ricompilati in piani Chronon,
  due item `image_chain` ciascuno, cinque immagini originali con SHA verificati.
  A ogni preset sono stati assegnati 3 run da 24 frame (frame 48–71), Vulkan
  + `h264_nvenc`. Candidato p1 usa CQP QP23; controllo P4 usa `driver-default`,
  come il controllo storico. I 24 frame prodotti sono ripetibili byte-per-byte
  entro piano e configurazione.
- `[MISURA]` p50 per piano: end-to-end 11,27 FPS con p1 contro 10,30 con P4
  (+9,4%); encoder open NVENC circa 31–35 ms in entrambi, perché ogni run apre
  un processo nuovo senza riuso. P1 produce file più grandi del 15,8–16,3%.
  SSIM sui 24 frame abbinati: 0,994258–0,994277.
- `[NOT RUN]` Questo è un campione di 8 item caption/image-chain, senza mappe;
  il rate-control non è identico tra p1/CQP e P4 driver-default. Non esiste nel
  ticket un budget bitrate numerico approvato: la misura quantifica il tradeoff,
  non autorizza l'attivazione della configurazione p1. Non verifica il writer
  P4 segmentato né la generazione overlay end-to-end.
- Sidecar, MP4 e log per run:
  [`A/B p1/P4 sui piani r2/r3`](../../../ops/jobs/asset_decode_corpus_replay_20261121/REPORT.md).

### Stato operativo del flag — 2026-11-21

- `[CODICE]` Il daemon corrente imposta `CHRONON_ENCODER_SESSION_REUSE=1`
  quando la variabile non è definita (`daemon_service.cpp`, inizializzazione
  del servizio); un valore esplicito `0` resta il kill-switch.
- `[CODICE]` La sessione viene comunque adottata solo per configurazioni
  CQP ammesse dal fingerprint. P4 driver-default e CRF non usano il riuso.
- `[MISURA]` Il benchmark 100-job CQP documenta +20,3% throughput e parità
  pixel, ma usa un piano sintetico già pronto. Non certifica la qualità p1
  sul corpus editoriale e non chiude il gate P4 segmentato.
- **Conseguenza:** il lavoro residuo non è abilitare globalmente il flag; è
  decidere il preset p1/CQP sulla base di soglie qualità/bitrate e validare
  l'adozione su 10 item rappresentativi, oppure misurare il limite P4.

### A/B renderer su mix con mappe semantiche — 2026-11-21

- `[MISURA]` 100 job locali per run, tre run per variante, Vulkan/NVENC CQP
  QP23/p1, 90 piani immagine e 10 piani semantici ModernMap1 da 24 frame,
  due producer IPC. Mediana 141,88 item/min con riuso contro 123,52 senza
  (+14,9%); 99/100 adozioni per run. Ogni configurazione è ripetibile per
  SHA tra i tre run.
- `[MISURA — PARITÀ FAIL]` Confronto decoded framemd5: 2.285/2.400 frame
  pixel-identici. I 115 frame diversi sono i frame 2–24 di cinque job mappa
  (item 0, 10, 20, 30 e 40); i restanti cinque job mappa e tutti i 90 job
  immagine sono identici. SSIM medio dei dieci job mappa 0,999996; cinque
  sono 0,999992 e cinque 1,0. La differenza è ripetibile ma il ticket richiede
  parità esatta o una soglia qualità e revisione visiva approvate: non si
  promuove il gate a PASS sulla sola SSIM.
- `[NOT RUN]` Nessun budget bitrate/SSIM approvato, revisione visiva formale,
  root cause dell'encoder rispetto alle mappe, worker end-to-end o writer P4
  segmentato. Benchmark locale e limiti in
  [`throughput_map_assets_20261121`](../../../ops/jobs/throughput_map_assets_20261121/REPORT.md).

### Replay worker ModernMap1 — 2026-11-21

- `[MISURA — PARITÀ FAIL]` Il replay queue-to-worker P5 ripete gli stessi piani
  semantici in tre batch da 100 job. Le mappe divergono in modo sostanziale tra
  run: esempio SSIM decoded 0,894924, con il fill rosso assente da frame 10 in
  uno degli output. Anche plan/item ID univoci non rendono gli output
  deterministici. Questo errore è più grande della piccola differenza vista
  nell'A/B encoder locale e non è attribuito al codec senza ulteriore
  isolamento.
- `[MISURA]` Il worker ha completato 300/300 job senza retry e con decode
  verificato; il batch è stato `object_store_only`. Dati e sample sono in
  [`worker P5 2026-11-21`](../../../ops/jobs/chronon3d_worker_p5_20261121/REPORT.md).
- `[NOT RUN]` Isolamento encoder vs renderer, gate qualità/bitrate p1/P4 e
  prova di parità worker dopo una correzione. Il riuso resta un guadagno
  misurato, non una chiusura di equivalenza per contenuti animati.

---

## 5. Demolition Debt

| Campo | Valore |
|---|---|
| **Owner** | maintainer encoder video. |
| **Reason** | Il percorso p1/CQP può rendere inutile il ramo P4 per l'overlay; il ramo P4 resta per i job che lo richiedono. |
| **Exit condition** | La configurazione scelta è una sola per il job overlay e il ramo alternativo non è raggiunto (`codec_session_config_supported=false` conta 0 sul corpus). |
| **Equivalence test** | Parità qualità e MP4/frame hash sul corpus. |
| **Removal scope** | Flag/env e ramo di configurazione non più usati per l'overlay; non il supporto P4 in generale. |
| **Status** | `ACTIVE` durante la migrazione. |

Nessuna nuova cache: il riuso usa il `CodecSession` già esistente.
