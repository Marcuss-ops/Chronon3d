# TICKET-CANONICAL-X264-SINGLE-SESSION — Chronon emette lo stream x264 canonico in un'unica sessione

- **Stato:** CHIUSO (2026-11-16) — misurato e non conveniente come default: la lane canonica in sessione costa **39,86 s** contro **5,81 s** di NVENC + post-pass sullo stesso piano 49 frame; default NVENC+post, lane disponibile solo opt-in.
- **Priorità:** P2 (costo CPU/IO sul finalize; non blocca la release)
- **Data:** 2026-11-16
- **Ambito:** Chronon3d — encoder native sink, mux session, render receipt. Consumatore: RenderingGen (`canonicalizePreparedOutputs`)
- **Owner:** Chronon3d encoder/muxer owner
- **Coordina:** [`TICKET-FINAL-JOB-BITSTREAM-REUSE-2026-11-01`](../../../../refactored/docs/operations/TICKET-FINAL-JOB-BITSTREAM-REUSE-2026-11-01.md) (P1/P5); RenderingGen [`canonical_overlay.go`](../../../../RenderingGen/renderinggen/internal/processor/canonical_overlay.go)
- **Tipo:** engine feature + certificazione di byte-identità; nessun cambio di schema RenderPlan, nessun secondo renderer

> Evidenza di codice letta sull'albero di lavoro al 2026-11-16. Non è una
> certificazione same-SHA: l'albero contiene modifiche non committate.

---

## 1. Problema

Per i profili `VELOX_OVERLAY_VIDEO_ONLY_V3` e `VELOX_ASSEMBLY_READY_V1` il
deliverable non è ciò che esce da NVENC: RenderingGen ri-encoda ogni artefatto
con libx264 nella post pool, dopo che la lane GPU ha rilasciato il job
(`canonicalizePreparedOutputs`, `internal/processor/canonical_overlay.go`). La
ragione è misurabile: NVENC emette parameter set diversi sotto lo stesso profilo
nominale (avcC live `111186fe…` contro il canonico `7201ce80…`), e il bitstream
canonico è un'identità di byte, non una dichiarazione.

Costo misurato sul dev host (24 core, clip 19 s 1080p24, 456 frame, 25 MB):

| Voce | Misura |
|---|---:|
| encode canonico libx264 `veryfast` CRF 23 | **5,5 s** per 19 s di timeline |
| di cui nel batch segmentato | **×2**: il mirror e i segmenti coprono la stessa timeline |
| probe della sorgente (prima del fix 2026-11-16) | 0,086 s per artefatto |
| `-count_frames` per segmento (prima del fix 2026-11-16) | 3,84 s per 19 s di contenuto |
| secondo SHA-256 (prima del fix 2026-11-16) | 0,14 s per 25 MB |

I probe, il conteggio frame e il doppio hash sono già stati rimossi dal lato
RenderingGen. **Resta l'encode**, che è il costo dominante: un batch da 19 s paga
~11 s di x264 (mirror + segmenti) dopo aver già pagato NVENC. Il mirror esiste
solo perché serve un artefatto unico oltre ai segmenti: la seconda encode è
interamente lavoro duplicato sulla stessa timeline.

## 2. Perché il concat non è la risposta

La scorciatoia "concat dei segmenti canonici per produrre il mirror" è stata
provata e misurata, non stimata. Il risultato:

- avcC identico al canonico (`7201ce80…`, 49 byte), timebase 1/90000, PTS 0,
  456 frame — il profilo V3 passa;
- **ma** la packet table perde la cadenza: keyframe a …193 e 229, quindi gap 36
  al giunto invece di 48, e `ClosedGOP=false`.

Il censimento a valle (`refactored/internal/app/wiring/final_job_segment_prepare.go`)
decide la copia proprio dalla packet table (closed GOP, intervallo keyframe):
un mirror a cadenza non uniforme sposta il costo a valle, non lo elimina. Per
questo il concat è escluso e la strada è l'emissione diretta.

## 3. Obiettivo

Chronon emette **in una sola sessione** lo stream H.264 canonico, così che
l'artefatto sia già il deliverable e RenderingGen non debba ri-encodarlo.
In un batch segmentato la sessione emette una volta il mirror e una volta i
segmenti, ma ogni frame è codificato **una sola volta** dal produttore; il
post-lane non esegue più x264.

Il successo non è "la post pool è più veloce": è **zero encode x264 nel
finalize** con lo stesso bitstream certificato. Il costo x264 si sposta nella
sessione di render; il guadagno è l'eliminazione della passata NVENC
intermedia e del file intermedio (scrittura, lettura, trasferimento).

## 4. Contratto misurato (vincoli di accettazione)

L'identità non è "High 4.0": è l'avcC esatto. Questi valori sono pinnati da
`RenderingGen/renderinggen/internal/media/profile.go` (V1 e V3) e dal contratto
assembly; non sono negoziabili in questo ticket.

| Dimensione | Valore richiesto |
|---|---|
| avcC / SPS-PPS SHA-256 | `7201ce80a2b2b32a0c1a89501851099f267c1767d59ff799db744f72032aae5a` (49 byte) |
| codec / profilo / livello | H.264 / High / 4.0 |
| geometria / fps | 1920×1080, 24/1 CFR |
| pixel format / SAR | yuv420p / 1:1 |
| B-frame / reference | 0 / 1 |
| GOP | 48, **chiuso**, `keyint=48`, `min-keyint=48` richiesto (x264 lo clampa a 25), `scenecut=0`, `open-gop=0` |
| primo frame | IDR a PTS 0 |
| colore | `tv` / `bt709` / `bt709` / `bt709` |
| timescale traccia video | 1/90000 |
| rate control / preset | CRF 23 / `veryfast` |
| audio (solo V1) | AAC-LC 48 kHz, stream copy |
| container | MP4 |

La ricetta di riferimento è `canonicalOutputFFmpegArgs` in
`RenderingGen/renderinggen/internal/processor/canonical_overlay.go`:
`-c:v libx264 -preset veryfast -crf 23 -refs 1 -profile:v high -level:v 4.0
-pix_fmt yuv420p -g 48 -bf 0 -sc_threshold 0
-x264-params scenecut=0:open-gop=0:keyint=48:min-keyint=48
-colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv
-video_track_timescale 90000`.

Nota sul clamp: x264 limita `min-keyint` a `keyint/2 + 1` (25 con
`keyint=48`). Lo fa anche la ricetta FFmpeg di riferimento, quindi il valore
effettivo è 25 in entrambe; la cadenza resta esattamente 48 perché
`scenecut=0` e `keyint=48`, e l'avcC non cambia. L'hash certificato resta
l'oracolo, non il valore nominale richiesto.

## 5. Stato del codice (cosa c'è, cosa manca)

C'è già molto, nel native sink:

- `Chronon3d/src/media/video/encode/native_av_sink.cpp` (blocco H.264) —
  `gop_size` dalla authority condivisa (48 a 24 fps), `max_b_frames = 0`,
  `refs = 1`, High/4.0, SAR 1:1, `AV_CODEC_FLAG_CLOSED_GOP`, BT.709 limited
  range;
- `native_av_sink.cpp` (AVOptions) — preset, CRF, tune;
- `Chronon3d/src/media/video/mux/packet_assembler_session.cpp`
  (`MuxSession::open`) — la traccia video eredita il timebase del codec (o
  1/1000); il timescale 90000 va passato esplicitamente;
- `Chronon3d/apps/chronon3d_cli/utils/video/encoder_bitstream_contract.cpp` —
  il contratto legge i parameter set dall'extradata **materializzato**
  dall'encoder, cioè la stessa verità che vede il decoder.

**Implementato il 2026-11-16** (engine lane):

- `VideoEncoderConfig::canonical_x264` in
  `include/chronon3d/media/video/video_config.hpp`: un solo flag, nessun
  parametro sparso.
- `NativeAvSink::open` (`src/media/video/encode/native_av_sink.cpp`) impone la
  ricetta completa (libx264, `veryfast`, CRF 23, refs 1, bf 0, GOP 48,
  `scenecut=0`, `min-keyint=48` richiesto, `open-gop=0`, High/4.0, yuv420p,
  SAR 1:1, BT.709 tv, `AV_CODEC_FLAG_GLOBAL_HEADER`) e rifiuta geometrie/fps/
  preset/CRF/tune/pixel format diversi invece di ri-tararli in silenzio.
- Verifica all'apertura: l'extradata libx264 è Annex-B; il sink ricostruisce
  l'`AVCDecoderConfigurationRecord` esatto (inclusa l'estensione High
  `fd f8 f8 00`) e ne confronta lo SHA-256 con `7201ce80…`. Un mismatch
  fallisce prima di scrivere il file.
- `MuxOpenConfig::video_time_base` ora vince sul timebase del codec
  (`packet_assembler_session.cpp`), così la traccia MP4 è 1/90000 mentre
  l'encoder resta a 1/24.
- Test: `chronon3d_native_video_sink_factory_tests`, **15/15 casi, 1662
  asserzioni PASS** (2026-11-16, Release, build `-j 4`). Il caso canonico legge
  l'MP4 scritto: 49 byte di avcC con hash `7201ce80…`, keyframe a 0 e 48 su 49
  pacchetti, `time_base=1/90000`, `video_delay=0`, yuv420p 1920×1080. Due casi
  negativi verificano il rifiuto di 1280×720 e di `H264Nvenc`.

**Completato il 2026-11-16 (integrazione e certificazione):**

1. **Selezione nella richiesta di render.** `OutputSpec::encoder_profile`
   viaggia in `output_spec` (CLI e IPC), il normalizer valida il vocabolario
   e pinna la ricetta (libx264, `none`, CRF 23, `veryfast`, QP/bitrate
   azzerati, fingerprint `out_encoder_profile`), e RenderingGen sa selezionare
   `canonical_x264` solo per V3 1920x1080@24 video-only
   (`canonicalEncoderProfileFor`); la selezione automatica è stata poi spenta
   dal verdetto A/B (§8, `canonicalSingleSessionEnabled=false`), quindi oggi
   la lane viaggia solo su richiesta esplicita. Un valore ignoto fallisce con
   `InvalidEncoderProfile` prima di aprire l'encoder.
2. **Il receipt pubblica l'avcC.** `MediaStreamProbe::codec_config_sha256`
   hasha l'extradata grezzo (`params->extradata`) nel probe in-process,
   `render_receipt_json.cpp` lo serializza come `media.codec_config_sha256`,
   e il fallback ffprobe usa `-show_data` con lo stesso parser hexdump. Il
   consumatore `needsCanonicalEncodeFromRenderReceipt` ricade sul probe, che
   conferma i byte e salta l'encode.
3. **La lane vive anche in `NativeAvEncoder`** (path produzione CLI/daemon):
   stessi parametri x264, SAR 1:1, colore BT.709 tv, timebase traccia
   1/90000, verifica dell'avcC dopo `avcodec_open2`, nessun riuso di sessione.
4. **Due difetti trovati dall'E2E e corretti:** senza `sample_aspect_ratio`
   l'Annex-B era 41 byte invece di 42 (VUI senza aspect ratio); la
   duplicazione dei frame statici replicava l'IDR anchor per tutti i 49
   frame (49 campioni identici, nessuna `stss`, ogni frame I). La lane
   canonica ora rifiuta `supports_duplicate_frames()` e codifica ogni frame.
5. **Il finalize non paga più né encode né probe** (2026-11-16).
   `receiptCertifiesCanonicalIdentity` in
   `RenderingGen/.../canonical_overlay.go` accetta come definitivo un
   artefatto il cui receipt passa la verifica, dichiara
   `media.codec_config_sha256 ==` hash del profilo e riporta la stessa
   dimensione del file. In quel caso `canonicalizeOutputMP4` non lancia
   ffmpeg e non lancia ffprobe: il probe di conferma era l'ultima lettura
   duplicata del finalize. Ogni incertezza (receipt assente, `fail`, senza
   digest, avcC diverso, size mismatch, profilo che non pinna l'avcC) ricade
   sul probe, quindi il percorso resta fail-closed e idempotente.
6. **Tutti i gate raggiungibili chiusi il 2026-11-16.** Gate 1 (V1 con
   audio): `TestCanonicalizeAssemblyV1EmitsPinnedStreamAndKeepsAudio` PASS —
   avcC canonico, timebase 1/90000, audio AAC 48 kHz conservato, 120 frame,
   seconda passata idempotente. Gate 3 (456 frame): artefatto di produzione
   `canonical456.mp4` con 456/456 frame decodificati, 10 keyframe a delta 48,
   `stss=[1,49,…,433]`, avcC `7201ce80…`; test C++ dedicato con 477
   asserzioni. Gate 5: split 48+408 e concat `-c copy` → 456 frame, avcC e
   cadenza invariati. Gate 6: NVENC 49 frame produce avcC `111186fe…`
   (60 byte) e i fatti strutturali attesi in 4,69–4,94 s. Gate 4:
   `TestCanonicalizePreparedOutputsSkipsProbeAndEncodeWhenReceiptCertifies`
   PASS (payload non-MP4: qualunque probe/encode fallirebbe). Gate 2: fatti
   verificati sull'artefatto 456.

**Esito A/B e residui:**

- **A/B totale (§8) — FAIL, decide la chiusura.** Sullo stesso piano 49
  frame 1080p24, host scarico: NVENC **4,88 s** + post-pass canonica
  **0,93 s** = **5,81 s**; lane canonica in sessione **39,86 s**
  (`render_ms=33,1 s`, ~635 ms/frame di backend overhead). Il gate §8
  accetta la lane solo se il totale non peggiora: peggiora di ~6,9×. La
  selezione automatica è quindi spenta (`canonicalSingleSessionEnabled=false`)
  e il default resta NVENC+post; la lane resta disponibile solo con
  `output_spec.encoder_profile=canonical_x264` esplicito.
- **Telemetria P2** (sidecar `gop`/`scenecut`/`preset`/`crf`): non
  implementata e non necessaria alla decisione; il ticket si chiude qui.
- **Fallback ffprobe del receipt:** compilato; l'E2E ha esercitato il probe
  in-process (il path di produzione), non il fallback.
- **Fatti non popolati dal probe in-process:** `keyframe_interval` e
  `sar_num` restano 0 nel receipt; nessun gate li richiede e i fatti reali
  sono verificati da ffprobe sull'artefatto.
- **Segment writer P4:** la cadenza multi-GOP è certificata sull'artefatto
  456 frame; il writer segmentato non è stato rieseguito separatamente.

## 6. Implementazione proposta

### P0 — lane canonica nella sessione (obbligatoria per il guadagno)

1. [x] Aggiungere il profilo canonico all'authority esistente
   (`VideoEncoderConfig::canonical_x264`), con tutti i parametri implicati:
   `libx264`, `veryfast`, CRF 23, `refs=1`, `bf=0`, `gop=48`, `scenecut=0`,
   `min-keyint=48`, `open-gop=0`, High/4.0, yuv420p, SAR 1:1, BT.709 tv,
   timebase traccia 1/90000.
2. [x] Applicarlo in `NativeAvSink::open` e verificare l'avcC ricostruito
   contro l'hash certificato prima di aprire il muxer.
3. [x] Selezionarlo per V3 (1920x1080@24, video-only) nella richiesta di
   render (`output_spec.encoder_profile`, CLI e IPC); V1/V2 restano sulla
   post-pass e ogni altro output mantiene il default NVENC. Dopo l'A/B la
   selezione automatica è spenta: resta l'opt-in esplicito.
4. [x] Nessun fallback silenzioso: libx264 assente, geometria/fps diversi o
   avcC non corrispondente fanno fallire l'apertura con un errore esplicito.

### P1 — certificazione nel receipt (per eliminare anche il probe)

5. [x] Consumatore senza probe: `receiptCertifiesCanonicalIdentity`
   (`canonical_overlay.go`) salta encode e ffprobe quando il receipt
   certifica l'avcC del profilo per il file su disco; test
   `TestReceiptCertifiesCanonicalIdentity`,
   `TestCanonicalizeOutputMP4SkipsEncodeWhenReceiptCertifiesCanonical` e
   benchmark `BenchmarkCanonicalizeOutputDecision`.
6. Dopo la scrittura, pubblicare `media.codec_config_sha256` nel render
   receipt, calcolato **sugli stessi byte** che RenderingGen chiama avcC:
   SHA-256 dell'`AVCDecoderConfigurationRecord` (49 byte per questa lane), non
   dell'Annex-B SPS+PPS. Ricetta del consumatore:
   `hashFFprobeExtradata` (`internal/media/probe.go:448`) — SHA-256 dei byte
   grezzi dell'extradata riportato da ffprobe, colonna ASCII esclusa.
7. Aggiungere il campo a `render_receipt_json.cpp` accanto a `codec_profile`,
   `closed_gop`, `keyframe_interval`, con la stessa semantica "fatti dei byte
   finali".

### P2 — telemetria e guardia di regressione

8. [ ] Sidecar/receipt: `encoder_backend=libx264`, `avcc_sha256`, `gop`,
   `scenecut`, `video_time_base`, `preset`, `crf`. Non implementato: il
   ticket si chiude misurato e non conveniente; la telemetria non serve alla
   decisione.
9. [x] Test di regressione: la lane canonica confronta l'hash
   dell'extradata con `7201ce80…` (sink 16/16, E2E 49 e 456 frame) e due
   sessioni indipendenti emettono lo stesso digest di stream (avcC +
   PTS/DTS/flags/size/payload di ogni pacchetto). [x] La lane NVENC è stata
   rieseguita: avcC `111186fe…` e fatti strutturali invariati.

## 7. Gate di accettazione

Tutti i gate vanno eseguiti sull'artefatto **scritto**, non sulle opzioni.

**Evidenza 2026-11-16** (artefatti `canonical49.mp4` e `canonical456.mp4`,
1920x1080@24, lane `--encoder-profile canonical_x264`; test V1 con audio;
lane NVENC di default):

| Gate | Esito |
|---|---|
| 1. avcC | **PASS** — 49 byte, SHA-256 `7201ce80a2b2b32a0c1a89501851099f267c1767d59ff799db744f72032aae5a` |
| 2. fatti stream | **PASS** — 1920x1080, High, level 40, yuv420p, 24/1, SAR 1:1, `has_b_frames=0`, `refs=1`, `time_base=1/90000`, color `tv/bt709/bt709/bt709`, primo frame IDR a PTS 0 |
| 3. cadenza | **PASS** — 456 frame: 10 keyframe a delta 48 (`stss=[1,49,…,433]`); 49 frame: 2 I + 47 P, keyframe 0/180000 |
| 4. zero encode e zero probe finalize | **PASS** — `TestCanonicalizePreparedOutputsSkipsProbeAndEncodeWhenReceiptCertifies` (payload non-MP4, metrica canonicalize assente); il worker completo non è stato eseguito ma il punto di decisione è coperto |
| 5. concat al giunto | **PASS** — split 48+408 al confine GOP e `-c copy`: 456 frame, avcC invariato, 10 keyframe a delta 48, stesso `stss` |
| 6. regressione NVENC | **PASS** — 49 frame: avcC `111186fe…` (60 byte), High/40, 1920x1080, yuv420p, 24/1, SAR 1:1, 4,69–4,94 s |
| 7. A/B totale (§8) | **FAIL** — canonico 39,86 s contro NVENC+post 5,81 s; la lane non è accettata come default |

Il receipt dell'artefatto riporta `verification.status=pass`,
`copy_eligible=true`, `media.codec_config_sha256=7201ce80…`,
`video_time_base=1/90000`, `closed_gop=true`.

1. **Byte-identità avcC.** `ffprobe -show_streams -show_data` → SHA-256
   dell'extradata uguale a `7201ce80…ae5a`. Stesso esito per V1 (con audio) e
   V3 (video-only).
2. **Fatti di stream.** 1920×1080, High, level 40, yuv420p, 24/1, SAR 1:1,
   `has_b_frames=0`, `refs=1`, `time_base=1/90000`, color
   `tv/bt709/bt709/bt709`, primo frame IDR a PTS 0.
3. **Cadenza.** Ogni delta tra keyframe consecutivi = 48, closed GOP true
   sulla packet table; il fixture da 456 frame deve dare 456/456 frame
   decodificati senza errori.
4. **Zero encode nel finalize.** Con l'artefatto emesso da Chronon,
   `FinalizeJob`/`canonicalizePreparedOutputs` restituisce `changed=false`,
   nessun processo `ffmpeg` x264 nel post-lane, metrica
   `canonicalize_overlay_us` ≈ solo probe (o assente, se P1 è pronto).
5. **Concat al giunto.** Due segmenti consecutivi concatenati con `-c copy`
   devono mantenere closed GOP e cadenza 48 al confine — è il gate che il
   mirror per concat ha fallito (193→229, gap 36).
6. **Regressione NVENC.** La lane NVENC di default produce gli stessi fatti
   strutturali di prima (non lo stesso hash: NVENC non è bit-exact tra run).

## 8. A/B obbligatorio: dove finisce il tempo

Spostare x264 dentro la sessione **non riduce il tempo x264**: lo sposta. Oggi
la post pool lo esegue dopo aver liberato la GPU, potenzialmente in parallelo
tra job. Dentro la sessione, x264 occupa lo slot di render più a lungo e
compete con il compositor per la CPU. Il confronto da fare è sul **tempo totale
e sulla saturazione dello slot**, non sul solo post-lane:

| Configurazione | Render | Post-lane | Totale |
|---|---:|---:|---:|
| oggi (NVENC + x264 post) | NVENC | 5,5 s / 19 s | base |
| canonica (x264 in sessione) | x264 | ~0 | da misurare |

Gate: la configurazione canonica è accettata solo se il totale (render +
finalize) non peggiora e la saturazione degli slot non cala. Se peggiora, la
decisione è di prodotto: tenere NVENC + post-lane, oppure usare la lane
canonica solo per gli output che devono essere copy-eligible. In quel caso
questo ticket va chiuso come "misurato e non conveniente", non implementato
a metà.

**Esito misurato 2026-11-16 (host scarico, stesso piano 49 frame):**

| Configurazione | Render | Post-lane | Totale |
|---|---:|---:|---:|
| NVENC + x264 post | **4,88 s** | **0,93 s** | **5,81 s** |
| x264 in sessione | **39,86 s** | ~0 | **39,86 s** |

La lane canonica paga ~635 ms/frame di backend overhead nel render loop
(`render_ms=33,1 s`), quindi il totale peggiora di ~6,9×. Il gate §8 dice che
in questo caso la decisione è di prodotto e il ticket va chiuso come
"misurato e non conveniente, non implementato a metà": è quello che è stato
fatto. Default: NVENC + post-pass. La lane resta nel codice, con la ricetta
certificata e i test bit-for-bit, dietro `canonical_x264` esplicito;
`canonicalSingleSessionEnabled` è l'unico interruttore da riaccendere se
l'overhead per frame viene rimosso e l'A/B torna a favore.

Misure di supporto: sessione canonica 49 frame **1042,37 / 1078,03 ms**
(sink, due run, digest identico `f4954142…7417`); probe di conferma soppresso
**48,98 ms/artefatto** contro **0,098 ms** della decisione da receipt (≈500×);
artefatto 456 frame **350,15 s** in produzione, 456/456 frame decodificati.

## 9. Non obiettivi

- Non cambiare la lane NVENC di default per output non canonici.
- Non riscrivere SPS/PPS negli MP4 esistenti: le slice assumono quei
  parametri (vedi P5 di `TICKET-FINAL-JOB-BITSTREAM-REUSE`).
- Non far fidare RenderingGen di una dichiarazione non verificata: finché il
  receipt non porta l'avcC, il probe resta; il probe è economico (0,086 s) e
  il gate resta fail-closed.
- Non eliminare il post-lane in questo ticket: prima si emette, poi si
  dimostra che il gate salta l'encode. Il codice del consumatore è già
  pronto a rispettare un avcC canonico dichiarato dal receipt.
- Non usare il concat dei segmenti come scorciatoia: misurato, perde il
  closed GOP al giunto.

## 10. Domande aperte

1. **Chi seleziona la lane?** Risposto e poi ridiscusso dall'A/B:
   RenderingGen sa richiedere `canonical_x264` per V3 1920x1080@24 video-only
   e Chronon valida/applica la ricetta fallendo chiuso, ma la selezione
   automatica è spenta (`canonicalSingleSessionEnabled=false`) perché il
   totale misurato peggiora. Resta l'opt-in esplicito in
   `output_spec.encoder_profile`.
2. **Un solo encoder o due?** Se il render usa x264 per il deliverable, il
   preview può restare NVENC? Oggi non è richiesto; non introdurlo senza una
   misura.
3. **Audio.** Risposto: `TestCanonicalizeAssemblyV1EmitsPinnedStreamAndKeepsAudio`
   verifica audio AAC 48 kHz a 1 stream dopo la canonicalizzazione V1.
4. **Segmenti.** Il P4 segmented writer tiene un encoder aperto per tutto il
   super-batch: con x264 il timebase 1/90000 e il GOP 48 devono valere anche
   per ogni segmento, non solo per il mirror. La cadenza multi-GOP è
   certificata sull'artefatto 456 frame; il writer P4 non è stato rieseguito
   separatamente (residuo dichiarato).

## Riferimenti

- Consumatore: `RenderingGen/renderinggen/internal/processor/canonical_overlay.go`,
  `internal/media/profile.go` (V1/V3), `internal/chronon/telemetry.go`
  (`Media.CodecConfigSHA256`).
- Misura del concat: `internal/media/zz_tmp_concat_probe_test.go` (rimosso dopo
  l'esperimento; risultato: avcC corretto, keyframe 193→229, `ClosedGOP=false`).
- Contratto canonico e storico: `TICKET-FINAL-JOB-BITSTREAM-REUSE`, P1/P5.
- Implementazione: `apps/chronon3d_cli/utils/video/native_av_encoder_open_negotiate.cpp`,
  `native_av_encoder_open.cpp`, `native_av_encoder_open_start.cpp`,
  `src/verification/render_receipt{,_probe,_json}.cpp`,
  `RenderingGen/renderinggen/internal/processor/canonical_encoder_profile_test.go`.
- Encoder: `Chronon3d/src/media/video/encode/native_av_sink.cpp`,
  `Chronon3d/apps/chronon3d_cli/utils/video/encoder_bitstream_contract.*`,
  `Chronon3d/src/media/video/mux/packet_assembler_session.cpp`.
