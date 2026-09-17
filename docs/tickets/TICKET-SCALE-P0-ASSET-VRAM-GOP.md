# TICKET-SCALE-P0 — pre-scaling P0 closure: shared asset identity, VRAM admission truth, single GOP executor authority

Stato: codice LANDED su `main` locale (build verde), evidenza testuale sotto.
Nessuna modifica ai canonici oltre la riga indice in
[`FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md).

Contesto: prima di alzare la concorrenza DirectYUV oltre il `mux` serializzato
di `daemon_service_ipc.cpp`, l'audit pre-scaling ha individuato quattro nodi P0
(lifetime degli asset condivisi, accounting VRAM, doppio executor GOP, doppia
policy `DeviceSelectionRequirements`) più due debiti MTSDF/Vulkan.

---

## P0-1 — `ImageCache` non trattiene più l'`AssetResolver` job-scoped

`VideoDeviceRuntime::ImageCache` è process/device lifetime e condivisa da tutti
i job DirectYUV di quel runtime; l'`AssetResolver` montato da un job è invece
job lifetime. Il percorso precedente installava il resolver del job nella cache
persistente:

```text
VideoDeviceRuntime (device)  →  ImageCache (device)  →  AssetResolver* (job)   ❌
```

Con un job solo restava nascosto; con due job concorrenti A poteva risolvere
con il root di B, e alla distruzione del job la cache tratteneva un puntatore
verso un oggetto distrutto.

Stato ora:

- `ImageCache::get_or_load(path, options, resolver)` /
  `find(path, options, resolver)` / `canonical_key_for(path, options, resolver)`
  sono il percorso canonico: la cache **non** trattiene il resolver, lo riceve
  per chiamata (`src/backends/assets/image_cache.cpp`,
  `include/chronon3d/backends/assets/image_cache.hpp`).
- `DirectYuvProgram::prepare()` usa l'overload esplicito
  (`src/media/video/direct_yuv/direct_yuv_program.cpp`).
- `setup_pipe_export_session()` non chiama più
  `image_cache().set_asset_resolver(job_ptr)`
  (`apps/chronon3d_cli/commands/video/exporters/pipe_export_pipeline_setup.cpp`).
- `set_asset_resolver()` resta solo per le cache per-runtime, dove cache e
  resolver hanno la stessa lifetime (`src/runtime/render_runtime.cpp`), ed è
  documentato come legacy da rimuovere.

Evidenza: `tests/cache/test_image_cache_resolver_identity.cpp` (4 case /
17 assertion, PASS) fissa che la risoluzione segue il resolver passato dal
chiamante e ignora quello installato, che due root diversi non aliasano la
stessa entry e che le `ImageDecodeOptions` partecipano della chiave.

## P0-2 — l'admission control descrive la VRAM reale

Il vecchio modello stimava `512 MB + 8 × w × h × 16` (≈1 GiB a 1080p) contro un
picco misurato di ~8.8 GB: un ordine di grandezza di errore, mascherato dal
mutex serializzato. Due correzioni:

1. `estimate_peak_vram_bytes()` /
   `resolve_device_requirements()` in
   `apps/chronon3d_cli/daemon/daemon_render_concurrency.hpp` sono l'unica
   policy: FullGraph scala dal punto osservato (8.8 GB @1920×1080, floor 2 GB),
   DirectYUV ha un floor conservativo di 1 GB, lavoro software dichiara 0.
2. La tabella locale del CLI pubblicava `vram_bytes = 0`
   (`video_export_pipe.cpp`), che **disabilita** il gate VRAM dello
   `DeviceScheduler` (guardia `capacity.vram_bytes > 0`): la stima onesta non
   avrebbe potuto fermare nulla. Ora il CLI sonda `cuDeviceTotalMem` e pubblica
   la memoria fisica, e `render_job_slots` viene dalla stessa authority
   (`resolve_render_job_slots`).

Effetto: su una scheda da 16 GB due job FullGraph a 1080p non entrano più
(2 × 8.8 > 16), e l'admission coincide con la realtà fisica.

Evidenza: 7 nuovi case in `tests/cli/test_daemon_render_concurrency.cpp`
(13 case / 94 assertion, PASS) fra cui "una scheda da 16 GB non può ammettere
due job FullGraph a 1080p".

### P0-2b — il working set deriva dal footprint compilato

La calibrazione da sola non è un modello: è un punto misurato. Il modello di
composizione è ora `derive_vram_working_set()` in
`apps/chronon3d_cli/daemon/daemon_render_concurrency.hpp`, unico per daemon e
CLI, con le stesse categorie di `graph::DeviceMemoryPlan`:

```
planned_physical_bytes            (ResourcePlan / CompiledResourceTable)
+ peak_live_bytes                  (picco transiente oltre il piano)
+ planned_physical_bytes × depth    (ring per-frame-slot, depth 6)
+ decoder_budget + encoder_budget + atlas_budget + scratch_budget
+ 15% margine
= derived_bytes

estimated_peak = max(derived_bytes, calibration_floor)
```

Due proprietà che il vecchio numero singolo non aveva:

1. **Il compiled non può sotto-promettere.** Il charge è il massimo fra il
   modello compilato e la calibrazione misurata, quindi la correzione P0-2 non
   regredisce se il footprint è piccolo.
2. **Il gap è visibile.** `calibration_gap_bytes()` / `calibration_gap_ratio()`
   riportano quanto del picco il piano compilato **non** spiega. A 1080p con un
   piano realistico (~40 MB) il termine derivato è ~0.3 GB contro un floor di
   8.8 GB: il gap è ~96% del charge. Questo è il numero che deve scendere prima
   di poter togliere il floor, ed è la misura onesta del perché il modello
   compilato **da solo non può** essere l'authority oggi.
3. **Under-admission è fail-loud.** `reconcile_vram_working_set(ws, admitted)`
   confronta il derivato con quanto è stato effettivamente riservato: se il job
   compila a più di quanto gli è stato ammesso, il verdetto è `UnderCharged` e
   `fail_loud()`. È one-sided per costruzione (col floor il charge è normalmente
   molto sopra il compilato), quindi è una rete di sicurezza, non l'unico
   controllo.

Il numero post-compile viene emesso dal percorso reale in
`apps/chronon3d_cli/utils/job/render_job_finalize.cpp`, dove il `ResourcePlan`
è già liftato dal `CompiledFrameGraph` (nessuna quarta copia del loop di
lifting) — l'INTERO plan, non una costante:

```
[INFO] vram working-set: compiled=..MB peak_live=..MB ring=..MB derived=..MB derived_from_compiled=1
```

Evidenza: 5 nuovi case in `tests/cli/test_daemon_render_concurrency.cpp`
(**18 case / 123 assertion, PASS**) che pinnano: modello senza footprint ==
calibrazione; il ring è `footprint × 6`; un footprint oltre il floor diventa il
charge ed è monotono; un job software senza NVENC non riceve calibrazione ma
possiede comunque il proprio piano; e i quattro verdetti di
`reconcile_vram_working_set` incluso `UnderCharged`/`fail_loud()`.

## P0-4 — una sola `DeviceSelectionRequirements`

Il daemon e il fallback CLI costruivano la richiesta con due logiche diverse
(sessione NVENC, codec, VRAM, PCIe). Ora entrambe chiamano
`resolve_device_requirements(...)`; il builder ad-hoc del CLI è rimosso.

## P0-3 — un solo executor per lo schedule GOP

`GopExecutionExecutor` è la authority dichiarata (`GopExecutionPlan` →
executor → una `MuxSession` caller-owned). La produzione CLI passava ancora da
`execute_hybrid_gop_plan` (ricostruzione `HybridSegment[]`, MP4 temporanei per
segmento, remux). In questo ticket lo schedule **valida** attraverso
`GopExecutionExecutor::execute()` prima di qualunque I/O: una schedule non
valida viene rifiutata dalla authority invece che accettata dalla
ricostruzione locale. La demolizione dell'I/O su filesystem resta debito
tracciato qui sotto.

> **Stato aggiornato (5G, chiusa):** lo snapshot sopra descrive la fase in cui il
> secondo executor era ancora vivo. Oggi `execute_hybrid_gop_plan`,
> `HybridSegment[]` e il workspace per segmento **non esistono più**: un solo
> executor, `execute_direct_mux_gop_plan`, scrive copie e re-encode nella stessa
> `MuxSession` caller-owned. La tabella *Demolition Debt* sotto riporta `DONE`.
> Le due voci residue della tabella *Non eseguito / residuo* sono state
> riconciliate (erano ferme allo snapshot di `e9615d889`).

Evidenza: la validazione è nel percorso di produzione
(`apps/chronon3d_cli/utils/video/video_job_execute_dispatch.cpp`) e compila nel
build video; l'equivalenza byte-per-byte è l'exit condition del debito.

## P0-5A — il gate di splice è encoder-owned, non più sorgente-contro-sorgente

Il preflight della sorgente compressa costruiva il target confrontandolo con sé
stesso. `make_bitstream_target_contract()` apriva `source_path` e
**sovrascriveva** codec, profile, level, dimensioni, pixel format, parameter
sets e color metadata con quelli della sorgente; subito dopo
`inspect_gop_source()` chiamava `compare_bitstream_compatibility(source_params,
target, ...)`. Il gate risultava quindi:

```text
source SPS/PPS  vs  source SPS/PPS   → compatible ✅   (tautologia)
```

mentre la domanda reale è:

```text
SOURCE bitstream  vs  ENCODER OUTPUT CONTRACT
```

Con il direct splice un falso positivo significa file che decodificano male ai
boundary. Ora il target è **encoder-owned** e la sorgente può essere solo il
lato `source` del confronto:

| Produttore | Dove | Cosa sa |
|---|---|---|
| **Encoder** (dichiarato) | `make_encoder_bitstream_contract()` in `encoder_bitstream_contract.{hpp,cpp}` | codec, dimensioni, color — dalle stesse `FfmpegPipeOptions` + `ResolvedEncoderConfig` che `NativeAvEncoder::open()` applica. profile/level/pixel format/parameter sets restano **unknown**: sono output dell'encoder, non configurazione |
| **Encoder** (materializzato) | `encoder_bitstream_contract_from_codec()` | profile, level e pixel format letti dal bitstream reale: `libavcodec` apre un decoder sull'extra data prodotto e riferisce i fatti che il demuxer consegnerà al playback decoder (nessuna inferenza da stringhe) |
| **Remux** (nessun encoder) | `make_remux_target_contract()` in `gop_smart_copy.cpp` | la lane whole-clip packet copy non ha encoder: lo stream del container *è* lo stream della sorgente. Nome e `producer` espliciti, raggiungibile solo dalla lane `gop_copy_only` |

`BitstreamTargetContract` porta ora `producer` (`Encoder`/`Remux`) e
`encoder_materialized`; `NativeAvEncoder::encoder_target_contract()` espone il
contratto materializzato per il direct mux (5C/5D) e lo azzera su `close()`.

### Cosa significa per la lane ibrida (fail-closed, dichiarato)

Il preflight avviene **prima** che esista un encoder, quindi il contratto
materializzato non è disponibile in quel punto: `profile`/`level`/parameter
sets restano unknown, `safe_to_splice()` è false e il resolver produce un
downgrade strutturato e contato (`gop_reencode_required`, `downgrade_count=1`)
invece di un copy non certificato. Non è una perdita silenziosa: è la stessa
rete di sicurezza già usata da `gop_splice_unsafe`.

**Reperto empirico (misurato su questa lane, `libx264`):** senza
`AV_CODEC_FLAG_GLOBAL_HEADER` l'encoder non espone i propri parameter sets nel
`AVCodecContext` — `extradata_size=0` anche dopo il primo frame, e l'avcC del
container viene sintetizzato dal muxer a partire dal bitstream in-band.
`NativeAvEncoder::open()` ora **pretende** l'header globale: un container deve
descrivere lo stream che porta, e la lane di splice ha bisogno che l'encoder sia
grado di dichiarare i propri parameter set invece di ripeterli su ogni
keyframe. Il contratto è quindi materializzato **già all'apertura** (codec,
dimensioni, configuration record dell'encoder), con `profile`/`level`/pixel
format esplicitamente **unstated** finché un consumatore li può derivare: i due
produttori del contratto (contesto codec e packet in-band, per gli encoder che
non onorano l'header globale) applicano ora **la stessa** regola di
materializzazione, altrimenti dichiarano due cose diverse sullo stesso stream.
La forma del contratto resta comunque fail-closed: un contratto **di sola
configurazione** (prima che un encoder esista) non è mai materializzato, e il
preflight continua a produrre il downgrade dichiarato `gop_reencode_required`.

Evidenza: 4 case in `tests/cli/test_packet_assembler_contract.cpp`
(`P0-5A: ...`) — contratto dichiarato encoder-owned e fail-closed; un target non
materializzato **non** certifica lo splice (il caso che prima era verde per
tautologia); la lane remux conserva i fatti della sorgente e pianifica ancora le
copie; il contratto materializzato descrive **esattamente** lo stream che il
demuxer rilegge dal container prodotto (parametri confrontati come NAL, non come
wrapper avcC-vs-Annex-B) e la stessa authority di riproduzione **rifiuta** uno
stream che l'encoder non ha prodotto.

Verifica sulla lane GPU reale (RTX A4000, `hevc_nvenc`, sorgente HEVC 3 GOP,
`--changed-interval 1.0:1.2:content`):

```text
gop preflight target: producer=encoder materialized=false
gop preflight: 3 GOPs (0 copy-eligible, 3 re-encode)
execution plan resolved (reason: gop_reencode_required}
final plan is a structured downgrade (reason: gop_reencode_required); count=1
native_av Opened: hevc_nvenc → output hevc Main 1280x720, 90 frame
```

Il downgrade è dichiarato, contato e produce comunque un artefatto valido: la
lane ibrida non certifica più uno splice non dimostrabile, e la sua riaccensione
dipende ora solo da 5D.

## P0-5B — parity harness del BoundaryReencode

La demolition (5G) richiede parità fra il percorso a segmenti e il direct mux.
Il **comparatore oracle** è ora in `tests/cli/test_gop_direct_mux_parity.cpp`:

- `gop_parity_corpus()` congela le forme che il gate deve coprire:
  `copy_reencode_copy`, `reencode_copy`, `copy_reencode`,
  `two_separate_modified_blocks`, `trim_plus_content_edit`;
- `probe_container()` estrae i fatti oracle: codec/profile/level/dimensioni/
pixel format/SHA-256 dell'extradata, e **per ogni packet** SHA-256 del payload,
PTS, DTS, duration, flags, più frame decodificati, durata e fatti audio;
- `compare_containers()` decide la parità. Per i packet copiati l'identità dei
byte è assoluta; l'SHA-256 dell'intero file è riportato **a parte** e non è mai
l'unico criterio (layout/metadati possono differire lecitamente).

Evidenza: 3 case — corpus congelato; un container identico ha zero differenze;
ogni classe di differenza (trim, re-encode, audio rimosso) è rilevata.

**CHIUSA (5E/5F/5G).** Il corpus non è più solo dichiarato: ogni forma viene
autorata su **una** fixture da 14 s (7 GOP della cadenza contrattuale di 2 s,
prodotta dallo *stesso* encoder che poi re-encoda, perché il gate pretende che
l'encoder riproduca lo stream dichiarato) e fatta girare due volte — direct mux
(copie + child encoder nel mux del chiamante + audio + un finalize) contro il
meccanismo a segmenti (`copy_gop_source` per unità copiata, un MP4 per unità
re-encodata, `assemble_segments`). Il test
`P0-5B: every frozen hybrid shape matches the segment-file oracle` verifica,
per **tutte e cinque** le forme:

```text
schedule    le kind pianificate sono quelle congelate (COPY/REENCODE, in ordine)
packet      stessa sequenza di payload (SHA-256), PTS, DTS, duration e flags
frame       stesse frame decodificate, decodifica OK, stessa durata container
copie       byte identity ASSOLUTA contro la SORGENTE, con il tick assegnato
            dallo schedule (più forte della parità con l'oracle)
audio       packet inviati esattamente una volta per l'intero schedule, contati
            dalla sorgente nella time base audio (una submit per unità
            moltiplicherebbe il conteggio)
```

Le finestre dei casi coprono `0..6` (COPY,RE,COPY), `2..6` (RE,COPY), `0..4`
(COPY,RE), `0..10` con **due** blocchi modificati separati, e `4..10` (trim +
content edit). La forma `trim` prova anche l'origine della finestra: il zero del
container è il primo tick richiesto, applicato a video **e** audio con lo stesso
intero.

## P0-5D — copy-to-mux nella MuxSession condivisa + parità end-to-end

Il percorso nuovo esiste ed è certificato contro l'oracle. Tre primitive, nello
stesso modulo `gop_smart_copy.{hpp,cpp}` (nessun secondo executor):

- `open_source_declared_mux(source, out, mux, include_audio)` — dichiara il
  container **dalla sorgente** (`MuxSession::open` accetta già `video_params`,
  quindi un packet copiato è corretto per costruzione) e restituisce i fatti
  che servono a certificare l'encoder contro lo stream dichiarato;
- `SourceMuxDeclaration::encoder_reproduces(encoder_contract)` — la
  certificazione di splice di questa lane, e usa **la stessa** authority di
  confronto (`compare_bitstream_compatibility`): nessuna seconda tabella;
- `submit_gop_source_packets(source, unit, mux, window_origin)` — scrive i
  packet di **un** `GopWorkUnit` usando `unit.source_packets` / `first_dts` /
  `last_dts` nella **time base della sorgente**, traslati sullo zero del
  container da un'origine in **tick interi** della stessa time base. Nessun
  `timestamp → secondi → frame → secondi → timestamp`: quella seconda
  interpretazione dello schedule è rimossa;
- `submit_source_audio(source, mux, video_window, window_origin)` — audio
  **una volta sola**, con la finestra dello schedule espressa nella time base
  **video** della sorgente, convertita con `av_rescale_q` e traslata dalla
  **stessa** origine: senza l'origine l'audio resterebbe sui suoi timestamp
  assoluti mentre il video parte da zero, cioè un offset A/V pari al trim;
- il chiamante finalizza **una** volta.

Vincolo di design risolto: i fatti bitstream dell'encoder esistono solo dopo il
primo frame encodato, quindi la certificazione non può avvenire a `open()`.
La lane direct-mux dichiara quindi il container dalla sorgente e **richiede** che
l'encoder riproduca quello stream; il gate non confronta mai la sorgente con sé
stessa (5A) perché il lato `target` è il contratto materializzato dell'encoder.

Evidenza (`P0-5D: source-declared direct mux matches the copy oracle`,
`tests/cli/test_gop_direct_mux_parity.cpp`):

```text
corpus reale: sorgente H.264 3 GOP (10 fps, GOP chiusi), all-copy

video:  packet count uguale, SHA-256 del payload uguale packet-per-packet,
        PTS/DTS/duration/flags uguali, extradata uguale, codec/profile/level/
        dimensioni/pixel format uguali
frame:  decodifica OK, stesso numero di frame decodificati
container: durata uguale
audio:  stesso numero di packet, stesso codec/sample rate/canali
SHA-256 dell'INTERO FILE: 6889c1a92591aecaddc8922528cdf1841c2b1b9203ded04b3d97b232362bed88
        oracle ≡ direct  (byte-identico, riportato a parte, mai unico criterio)
```

Il residuo **era** di wiring, non di primitive: in questo snapshot
`execute_hybrid_gop_plan` scriveva ancora un MP4 per segmento, e 5E/5F
significavano far scrivere il child render dentro la stessa `MuxSession` presa in
prestito (`FfmpegExportOptions` → `make_pipe_options` →
`FfmpegPipeOptions::borrowed_mux`) lasciando audio+trailer al chiamante, con 5G
legata a quella parità.

> **Chiuso.** 5E/5F/5G sono atterrate in `715c1220f`: il child scrive nel mux del
> chiamante, audio e trailer sono emessi una sola volta dal proprietario, la
> pubblicazione è il rename atomico di `.partial`, e `execute_hybrid_gop_plan`
> ha zero caller (zero occorrenze in `src/`, `apps/`, `include/`). Il percorso
> `encode → write → close → open → demux → timestamp-normalize → remux → delete`
> non esiste più. Resta solo **5H** (evidenza same-SHA su GPU reale).

---

## P0-GOP-HEVC — il NAL length di `hvcC` (l'intera lane HEVC era re-encode)

Trovato eseguendo il preflight 5A sulla lane GPU reale: **0 GOP copy-eligible su
ogni sorgente HEVC**, quindi `gop_copy_preflight_failed` e re-encode completo.
Non era causato da 5A: il binario `linux-video-release` **precedente** alle
modifiche dà lo stesso risultato.

Causa: `packet_is_certified_random_access()` leggeva la lunghezza dei NAL dal
record di configurazione con l'offset di **avcC** (byte 4) anche sui record
**hvcC**, dove `lengthSizeMinusOne` sta al **byte 21**. Misurato sul fixture:

```text
hvcC: 01 01 60 00 00 00 90 ...
  extradata[4]  & 3 + 1 = 1   ← offset avcC (quello che il codice usava)
  extradata[21] & 3 + 1 = 4   ← offset hvcC (corretto)
```

Con `length_size = 1` su un bitstream con prefissi a 4 byte il parse fallisce,
`random_access_certified` è sempre false, e **ogni GOP HEVC viene forzato a
re-encode**: la packet-copy lane è morta per il codec che la lane DirectYUV usa.

Correzione: `config_record_nal_length_size()` legge l'offset **per-codec**
(avcC byte 4, hvcC byte 21) e, quando un record valido esiste, la lettura
length-prefixed è **deterministica** e precede lo scan Annex-B — `00 00 01`
compare dentro slice data legittime, quindi dedurre Annex B dal payload
disclassificava stream validi.

Evidenza (stessa sorgente HEVC 1280x720, 3 GOP, `hevc_nvenc`, prima/dopo):

```text
prima: gop preflight: 3 GOPs (0 copy-eligible, 3 re-encode) → gop_copy_preflight_failed
 dopo: gop preflight: 3 GOPs (3 copy-eligible, 0 re-encode, all-copy) → explicit_gop_copy
       bitstream-copy: 90 video packets, 130 audio packets in 17.3ms
       ffprobe: hevc Main 1280x720, 90 packet letti (nessun NVDEC/NVENC/Vulkan)
```

Test di regressione: `HEVC length-prefixed GOPs are certified copy-eligible
(hvcC)` in `tests/cli/test_packet_assembler_contract.cpp` (fixture `libx265`,
quindi eseguibile anche senza GPU).

## P0-5C — `MuxSession` caller-owned (chiusa)

`FfmpegPipeOptions::borrowed_mux` + `borrowed_declared_stream` aggiungono la
proprietà caller-owned senza cambiare la lane normale (`nullptr` = l'encoder apre
e finalizza il proprio container). Con un mux preso in prestito:

- `NativeAvEncoder` distingue `owned_mux_` (proprietà) da `mux_` (il sink
  effettivamente scritto) e `borrows_mux_`;
- `close()` fa **solo** flush del codec: niente audio, niente trailer. Finalizzare
  lì chiuderebbe il container dopo il **primo** GOP ri-encodato, perdendo tutti
  i work unit successivi — esattamente il rischio elencato in apertura;
- nella stessa `close()`, **prima** di lasciare il container al chiamante,
  l'encoder viene certificato contro lo stream che il container dichiara
  (`encoder_reproduces_declared_stream`): se non lo riproduce, `close()`
  fallisce e il lavoro non viene pubblicato;
- la telemetria del container (`open_mux_header_ms`, `hash_metrics`,
  `full_file_reread_count`) resta al **proprietario**, senza doppio conteggio.

Una nota sul contenuto del container prestato, perché non è ovvio: la
**dichiarazione** dello stream viene dalla **sorgente** (è ciò che rende
corretto per costruzione un packet copiato), quindi il container porta già il
configuration record della sorgente. Il child encoder deve perciò emettere il
formato che quel container memorizza — vedi il punto 4 di 5E/5F.

Evidenza: `P0-5C: an encoder writes through a caller-owned MuxSession` — la
sorgente è prodotta dallo stesso encoder, il chiamante apre il container sulla
dichiarazione della sorgente, l'encoder scrive un frame, `close()` lascia il
container aperto **e certifica la riproduzione**, il chiamante finalizza una
sola volta, e il file prodotto è leggibile dal demuxer e contiene esattamente 1
packet H.264.

## P0-5I — misura VRAM DirectYUV (sostituisce il floor da 1 GB come unico dato)

Misura su host reale (RTX A4000 16 GB), esportazione DirectYUV 90 frame /
`hevc_nvenc`, campionando `nvidia-smi --query-compute-apps=pid,used_memory`
ogni 20 ms e attribuendo il picco **al processo del job**:

```text
720p  (1280×720)   solo       279 / 281 / 279 MiB    (3 repliche)
1080p (1920×1080)  solo       400 / 400 / 400 MiB    (3 repliche)
1080p              2 concorrenti  A=400 MiB  B=400 MiB
                                60 campioni realmente sovrapposti,
                                entrambi hanno raggiunto il loop direct-yuv
```

Due cose che il dato dice e una che non dice:

- il picco cresce **sub-linearmente**: 2.25× i pixel (0.92 → 2.07 MP) danno
  1.43× la VRAM (280 → 400 MiB). L'overhead fisso domina;
- due job concorrenti **non gonfiano** il footprint per job: 400 MiB ciascuno,
  identico al solo. Il contendere si vede nel tempo
  (`total_render` 410 ms → 2159 ms), non nella VRAM;
- 4K resta **NOT RUN**, e il numero è di **picco**, non di saturazione: una
  misura non sostituisce il modello `derive_vram_working_set()`.

Conseguenza: il floor DirectYUV di 1 GB in `daemon_render_concurrency.hpp` è
~2.5× il picco misurato a 1080p, quindi ha margine per essere abbassato — ma
questo ticket **non** rimuove `m_render_job_mutex`: un floor più basso cambia
l'admission, non la correttezza della concorrenza, che va decisa sul modello, non
sul picco di due job.

---

## MTSDF — identità content-addressed (e regressione di performance evitata)

`text::prepare_mtsdf_glyph()` è l'unica preparation authority backend-neutral;
il refactor recente ha unificato la duplicazione FreeType, ma la cache di
preparazione era ancora keyata sul **path** del font e il nuovo codice
calcolava lo SHA-256 dell'intero file **a ogni richiesta di glifo** (un face CJK
da 20 MB avrebbe letto 20 MB per glifo).

Ora:

- `text::prepared_font_content_digest(resolver, font_path)` è l'unica authority
  di identità: SHA-256 dei byte reali, memoizzato per (path canonico, size,
  mtime) — una lettura completa per versione di file, non per glifo
  (`src/backends/text/mtsdf_preparation.cpp`,
  `include/chronon3d/text/mtsdf_preparation.hpp`).
- `GlyphKey` usa il digest del contenuto; anche la chiave della face LRU lo
  include, altrimenti una face aperta sui byte vecchi genererebbe glifi poi
  salvati sotto il digest nuovo.
- Residenza face limitata (LRU 32, `CapacityMode::Count`) invece di una
  `unordered_map` illimitata.
- `DirectYuvTextTextureCache` (cache packed CUDA) usa la **stessa** identità
  tramite `key_for()`: `find`, `store` e `get_or_create` condividono una sola
  funzione di chiave, quindi la lowering non può più fare HIT sul path
  scavalcando il controllo di contenuto
  (`src/media/video/direct_yuv/text_texture_cache.{hpp,cpp}`).

Evidenza: `tests/text/test_mtsdf_font_identity.cpp` (5 case / 36 assertion,
PASS) e la suite preesistente `chronon3d_mtsdf_generator_tests` (7 case /
178 assertion, PASS) che dimostra che la generazione glifo non è cambiata.

Residuo dichiarato: `PreparedFontIdentity` non modella ancora l'istanza di
variazione (face index/variation axes) — nel repo non esiste una authority
delle variation, quindi la chiave usa face index 0. Va estesa quando la
selezione variability entra in scope.

## Vulkan — separazione planned/unplanned

`VulkanSurfaceAuthority` usa due domini numerici distinti: placement compilato
(`bind()`, slot plan-locali) e allocatore unplanned (`ensure()`,
`next_slot` da `kUnplannedSlotBase = 1 << 20`). La materializzazione compilata
resta compiler-owned e non può raggiungere il fallback dinamico. Non è
certificata same-SHA: resta il punto del debito sotto.

---

## Demolition Debt — hybrid GOP segment-file executor (P0-3)

| Campo | Valore |
|---|---|
| **Owner** | video/media lane (CLI video export) |
| **Reason** | `execute_hybrid_gop_plan()` è sopravvissuto come secondo executor: ricostruisce il grouping da `work_units`, ricrea i confini di segmento, decide copy/reencode, scrive un MP4 per segmento e poi rimuxa. Serve solo finché la scrittura diretta in una `MuxSession` non è certificata sul corpus. |
| **Exit condition** | `GopExecutionExecutor` + una sola `MuxSession` produce output byte-identico al percorso a segmenti sul corpus hybrid, e `execute_hybrid_gop_plan` ha zero caller. |
| **Equivalence test/gate** | `tests/cli/test_gop_direct_mux_parity.cpp` — corpus congelato (`gop_parity_corpus()`) + comparatore oracle (`probe_container()`/`compare_containers()`): identità di payload/PTS/DTS/duration/flags per i packet copiati, fatti di stream, frame decodificati, durata, audio; SHA-256 dell'intero file riportato a parte. La lane **copy** è già verde con SHA-256 identico (5D); l'exit condition copre però l'intero corpus hybrid, quindi serve il reencode nel mux condiviso (5E/5F). |
| **Removal scope** | `struct HybridSegment`, workspace `.hybrid-*`, `segment_paths`, il caller hybrid di `assemble_segments()`, `execute_hybrid_gop_plan()`, e i commenti di debito in `video_job_execute_dispatch.cpp`. |
| **Status** | `DONE` — **5A** (target encoder-owned), **5B** (gate di parità, cinque forme), **5C** (seam caller-owned), **5D** (copy-to-mux + parità), **5E/5F** (child render nel mux del chiamante, audio e trailer una volta) e **5G** (demolizione) landati. Il percorso `encode → write → close → open → demux → timestamp-normalize → remux → delete` non esiste più: `execute_direct_mux_gop_plan` è l'unico executor. Resta **5H** (evidenza same-SHA su GPU reale). |
| **Residuo cosciente** | `copy_gop_source()` **non** è stata rimossa: non è più un meccanismo a segmenti ma (a) l'implementazione della lane **whole-clip packet copy** (scrive il container finale, pubblicato con rename atomico) e (b) **l'oracle** del gate di parità. Il suo scope di rimozione non la elencava; eliminarla oggi toglierebbe l'oracle contro cui la lane ibrida è certificata senza sostituirlo con niente. Candidata a cadere quando la lane copy passerà anch'essa dalle primitive `open_source_declared_mux` + `submit_gop_source_packets` con un oracle congelato equivalente. |

## P0-5E/5F — un solo executor, un solo mux caller-owned

`execute_direct_mux_gop_plan` (`video_job_execute_dispatch.cpp`) è l'unico
executor della lane ibrida. Struttura:

```text
GopExecutionPlan (source intera)
   ↓ gop_units_in_window(plan, request_start, request_end)   ← una authority di selezione
unità della finestra
   ↓ GopExecutionExecutor
COPY (submit_gop_source_packets)  REENCODE (child nel mux prestato)
   ↓ av_interleaved_write_frame (una MuxSession)
submit_source_audio (una volta) → finalize (una volta) → verifica contratto → rename atomico
```

Punti che sono costati un difetto vero e sono ora fissati e testati:

1. **Il piano copre la sorgente intera.** La finestra *seleziona* le unità
   (`gop_units_in_window`, la stessa funzione usata dal gate di parità); un
   bordo che cade **dentro** un'unità copiata viene **rifiutato**
   (`window_start_not_gop_aligned` / `window_end_not_gop_aligned`) invece di
   emettere il GOP intero, perché il frame count pubblicato deve essere quello
   richiesto. Un'unità re-encodata invece viene resa esattamente per i frame
   dentro la finestra.
2. **Un solo zero per video e audio**: `request_start_pts` in tick interi,
   applicato ai packet copiati, all'origine del child (`set_video_origin`) e
   all'audio via `av_rescale_q`. Numeri interi in una time base, nessun secondo
   reintrodotto.
3. **L'ultimo packet di un child encode non aveva duration**: il container
   perdeva l'ultimo sample dalla durata (edit list più corta del filmato, e la
   demuxer marcava l'ultima frame come discard). Il confine del mux ora applica
   la convenzione "un packet senza duration vale un tick del produttore", la
   stessa che il vecchio `PacketAssembler` usava.
4. **Il container prestato dichiara il proprio formato.** Il child encoder
   emette Annex-B; poiché il container prestato porta **già** il configuration
   record della sorgente, libavformat non inserisce il BSF automatico di
   conversione e i packet sarebbero finiti grezzi nell'mp4 (file non
   decodificabile). Il producer converte (`convert_annex_b_to_length_prefixed`)
   esattamente quando il container dichiara un configuration record.
5. **Certificazione a `close()`** del child contro lo stream dichiarato
   (`encoder_reproduces_declared_stream`), fail-closed: un encoder che non
   riproduce lo stream del container non pubblica nulla.

---

## Verifica (2026-09-15, P0-5A/5B/5C/5D/5E/5F/5G — questo change set)

```text
linux-video-test (Vulkan OFF, CUDA interop OFF, FFmpeg ON, text OFF, ccache + ninja -j16)
  cmake --build --target chronon3d_cli_tests                        EXIT 0
  chronon3d_cli_tests                 180/180 case, 3574 assertion  PASS
  cmake --build --target chronon3d_native_video_sink_factory_tests  EXIT 0
  chronon3d_native_video_sink_factory_tests   8/8 case, 268 assertion PASS

gate di parità (tests/cli/test_gop_direct_mux_parity.cpp)
  P0-5D  copy lane vs oracle                        PASS (parità packet-per-packet)
  P0-5E/5F  COPY+REENCODE+COPY vs oracle            PASS (60/60 packet, 60 frame)
  P0-5B  cinque forme del corpus vs oracle          PASS (tutte, packet-per-packet)
  P0-5F  audio una volta sola (conteggio dalla sorgente) PASS
  P0-5A  contratto encoder-owned + authority di riproduzione PASS
  P0-5C  container prestato + certificazione a close PASS
```

Non-regressione: il gate di parità confronta **payload/PTS/DTS/duration/flags
packet per packet** contro il meccanismo a segmenti; l'SHA-256 dell'intero file
resta riportato a parte (il layout del container può differire lecitamente: nel
case ibrido i due file sono semanticamente identici ma non byte-identici).

## Verifica (2026-09-15, P0-5A + 5B + hvcC + 5C)

```text
linux-video-test (Vulkan + CUDA interop + FFmpeg + text + daemon, ccache + mold)
  ninja chronon3d_cli_video_export / chronon3d_cli / chronon3d_cli_tests   EXIT 0
  chronon3d_cli_tests                     177/177 case, 2022 assertion  PASS
    (9 nuovi: 4× P0-5A, 3× P0-5B, 1× hvcC, 1× P0-5C)

lane GPU reale (RTX A4000 16 GB, driver 595.91.07, CUDA 13.2)
  copy-only HEVC 3 GOP  → all-copy, bitstream-copy 90 video + 130 audio packet, 17.3ms
  hybrid intent HEVC    → gop_reencode_required, downgrade contato, hevc Main 90 frame
  DirectYUV 720p        → picco 285 MiB (baseline 5121 escluso)

gates
  python3.11 tools/check_architecture.py          GATE_PASS 191/191
  tools/check_test_hygiene.sh                     HYGIENE_PASS 3/3
  tools/check_doc_sha_dedup.sh                    DEDUP_PASS
  tools/check_conflict_markers.sh                 GATE_PASS
  tools/check_filename_drift.sh                   nessun drift nuovo sui file toccati
  tools/check_cmake_format.sh                     NOT RUN (cmake-format non installato)

build acceleration
  mold 1.0.3 — `-fuse-ld=mold` nelle regole di link della lane
  ccache — launcher su 1151 regole, 5064 richieste, 51.6% hit
```

Nota di non-regressione: la lane `linux-fast-dev` (VIDEO=OFF,
NATIVE_FFMPEG=OFF) non compila nessuno dei TU toccati (vivono in
`chronon3d_cli_video_export`, gated su `CHRONON3D_ENABLE_VIDEO`); il percorso
`#else` di `encoder_bitstream_contract.cpp` è stato verificato con
`g++ -fsyntax-only` senza `CHRONON3D_ENABLE_NATIVE_FFMPEG`.

## Verifica (2026-09-15, questo change set)

```
linux-video-fast-dev (Vulkan + CUDA interop + FFmpeg + text + daemon)
  chronon3d_cli                                  269/269  EXIT 0  0 errori
  (include tutti i TU toccati: pipe_export_pipeline_setup.cpp,
   video_export_pipe.cpp, render_job_finalize.cpp, daemon_service_ipc.cpp,
   e i consumatori di pipe_export_session.hpp / daemon_render_concurrency.hpp)

linux-fast-dev (tests ON) — eseguiti dalla RADICE del repo
  chronon3d_daemon_render_concurrency_tests    18 case / 123 assertion   PASS
  chronon3d_cache_tests                                     997574       PASS
  chronon3d_mtsdf_font_identity_tests                        36         PASS
  chronon3d_mtsdf_generator_tests                           178         PASS
  chronon3d_backend_registry_tests                          108         PASS
  chronon3d_compositor_tests                             613890         PASS
  chronon3d_render_job_contract_tests                       287         PASS
  chronon3d_render_graph_tests                  378 case / 18653       PASS

gates
  check_architecture.py                191/191 PASS
  check_test_hygiene.sh                    3/3 PASS
  check_doc_sha_dedup.sh                DEDUP_PASS
```

Nota di metodo: le suite test dipendono da path relativi
(`tests/fixtures/Inter-Bold.ttf`), quindi **vanno eseguite dalla radice del
repo**; lanciate da `tests/` producono falsi rossi (`REQUIRE(exists(kFont))`
fallisce in 7/7 e 1/5 case) che non hanno nulla a che vedere col codice.

## Non eseguito / residuo

| Item | Stato | Nota |
|---|---|---|
| Suite video-gated (DirectYUV text cache, executor GOP) | **PASS** | lane `linux-video-test` configurata e costruita (648/648 step, EXIT 0): `chronon3d_cache_tests` include ora `cache/test_direct_yuv_text_cache.cpp` (**997596** assertion, +22 rispetto al lane fast-dev) e `chronon3d_cli_tests` include `cli/test_packet_assembler_contract.cpp` (GOP executor). Focus mirati: **DirectYUV text cache 2 case / 5 assertion PASS**, **GOP 9 case / 122 assertion PASS**, **mux boundary 3 case / 11 assertion PASS** |
| Piano VRAM derivato dal footprint compilato | **PASS (modello) / PARTIAL (site)** | `derive_vram_working_set()` è l'unico modello di composizione: `planned_physical_bytes` + `peak_live_bytes` + ring (footprint × `kDefaultFrameRingDepth`) + budget NVDEC/NVENC/atlasi/scratch + margine 15%, le stesse categorie di `graph::DeviceMemoryPlan`. La calibrazione misurata è il **floor** che il charge non può sottopassare. Resta PARTIAL sul *site*: il payload `RENDER_JOB` porta solo `plan_path`, quindi al punto di admission pre-compile **non esiste** alcun footprint e il daemon usa legittimamente il floor; il numero post-compile è derivato dal grafo reale in `render_job_finalize.cpp` |
| Gate `estimated_peak_vram` vs `observed_peak_vram` | **PARTIAL** | `reconcile_vram_working_set()` rileva l'under-admission (compiled > charged: `verdict == UnderCharged`, `fail_loud()`), coperto da test. Il confronto dati-vs-stima su una run GPU-host resta **NOT RUN**: nessuna GPU in questa sessione |
| Lane `linux-video-test` | **LANDED** | liberati i due alberi `.tmp/chronon-builds/*` (33 GB; il daemon e `pipelinegen` girano da `build/chronon/linux-video-release`, nessun file aperto sotto `.tmp`), poi `cmake --preset linux-video-test` + build mirata: 648/648 step, EXIT 0, 19 GB liberi residui. Il vcpkg per-preset esisteva già (`vcpkg_installed/linux-video-test`), quindi nessuna reinstallazione di dipendenze |
| P0-5A gate encoder-owned | **LANDED + verificato su GPU** | Il target non è più derivato dalla sorgente su nessuna lane che ri-encoda; `producer` esplicito (`Encoder`/`Remux`), contratto materializzato esposto da `NativeAvEncoder`, fail-closed quando i fatti dell'encoder non esistono ancora. Su `hevc_nvenc` reale il preflight produce un downgrade strutturato `gop_reencode_required` e un artefatto valido |
| P0-GOP-HEVC NAL length `hvcC` | **FIXED** | `packet_is_certified_random_access()` leggeva l'offset avcC (byte 4) sui record hvcC (byte 21 corretto) → `length_size=1` invece di 4 → **0 GOP copy-eligible su ogni sorgente HEVC**, lane packet-copy morta per il codec usato da DirectYUV. Ora 3/3 GOP copy-eligible e `bitstream-copy` 90 packet in 17.3 ms; test di regressione con fixture `libx265` (CPU-only) |
| P0-5B parity comparator | **LANDED** | Corpus congelato + comparatore oracle (`tests/cli/test_gop_direct_mux_parity.cpp`): un container identico ha zero differenze e ogni classe di differenza (trim, re-encode, audio rimosso) è rilevata |
| P0-5C seam `MuxSession` caller-owned | **LANDED** | `FfmpegPipeOptions::borrowed_mux` + `owned_mux_`/`mux_`/`borrows_mux_`: in prestito `close()` fa solo flush del codec (niente audio, niente trailer) e la telemetria del container resta al proprietario. Test dedicato con 16 assertion |
| P0-5D copy-to-mux + parità end-to-end | **LANDED** | `open_source_declared_mux` + `encoder_reproduces` + `submit_gop_source_packets` + `submit_source_audio`: i packet copiati entrano nella `MuxSession` condivisa dalla time base della sorgente e il gate oracle passa — **SHA-256 dell'intero file byte-identico** fra i due percorsi, oltre a identità payload/PTS/DTS/flags, frame decodificati, durata e audio |
| P0-5E/5F (reencode nella stessa sessione, audio/finalize una volta) | **LANDED (`715c1220f`)** | Il collegamento è completo: `FfmpegExportOptions::borrowed_mux` → `make_pipe_options` (`pipe_export_helpers.cpp:139-141`) → `FfmpegPipeOptions::borrowed_mux`; in prestito `NativeAvEncoder::close()` fa solo flush del codec e certifica la riproduzione dello stream, mentre audio (`submit_source_audio`) e trailer (`mux.finalize()`) sono emessi **una volta** dal chiamante in `execute_direct_mux_gop_plan`. Verificato ora sul parity gate: `P0-5E/5F: the direct hybrid mux matches the segment-file oracle` PASS |
| P0-5I picco VRAM DirectYUV | **PASS (720p + 1080p + 2 job) / 4K NOT RUN** | 279–281 MiB @720p, 400 MiB @1080p (`hevc_nvenc`, 3 repliche ciascuno, campionamento per-PID), e **400 MiB per job** con due job realmente sovrapposti. Il floor di 1 GB ha ~2.5× margine a 1080p; la rimozione di `m_render_job_mutex` resta una decisione sul modello, non sul picco |
| P0-3 fasi 2/3 (mux diretto, cancellazione segmenti) | **LANDED (`715c1220f`)** | `render_and_encode_ffmpeg_pipe` **riceve** la `MuxSession` caller-owned via `borrowed_mux` e scrive lì sia i packet copiati sia quelli NVENC; il lato reencode è certificato a `close()` contro lo stream dichiarato (fail-closed). Exit condition raggiunta: parità sul corpus ibrido (5 forme su 5) e `execute_hybrid_gop_plan` a zero caller. Residuo dichiarato: `copy_gop_source()` resta come lane whole-clip **e** come oracle del gate (il suo scope di rimozione non la elencava), candidata a cadere quando anche la lane copy passerà dalle primitive |
| Certificazione same-SHA main + Vulkan planned/unplanned | **NOT RUN** | `CURRENT_STATUS` resta valido: main non è "green" senza evidenza same-SHA |
| `compatibility_composition` / C22→C23 | **OPEN** | invariato da questo ticket |
| `chronon3d_render_graph_tests` | **RESOLVED — 378/378 PASS** | Non causato dal change set P0. Il `REQUIRE` aggiunto ha sbloccato la run (378 casi invece di 31, la `CHECK` indicizzava un vettore vuoto) ed è emerso un terzo rosso latente. Entrambe le cause diagnosticate con esperimenti e chiuse secondo la decisione presa: vedi §Diagnosi layer-batch |

### Diagnosi layer-batch (esperimenti riproducibili)

Domanda: il lowering è sbagliato o il test è stale? Evidenza:

1. `a78a8a29e` **non** è coinvolto. Ripristinando la sola versione pre-`a78a8a29e` di
   `frame_graph_builder_program_detail.hpp` (backup in `.tmp/diag/program_detail.before.hpp`)
   e ricompilando: entrambi i fallimenti restano **identici** — stesso hash
   `0x65304b41dbf40d8f` e stesso `layer_batches.size() == 0`.
2. La causa è il gate `const bool frame_bound_image_source = source_is_image &&
   node_info.binding_meta.has_binding(); if (frame_bound_image_source) return false;`
   introdotto da `00be928ab` (2026-09-14). Rimuovendo **solo** quelle due righe
   (mantenendo `batch_is_sound`): **17/17 assertion PASS** — l'invariante
   `100 Image layers → 1 CompiledLayerBatch` e il golden committato
   `fnv1a64=0x610ce49755184814` (rigenerato 2026-08-27) tornano verdi.
3. `frame_graph_builder_metadata_detail.hpp` imposta `binding_meta.active = true`
   per **ogni** nodo `Source`/`TextRun` raggiungibile: sia il ramo
   `has_layer_location` sia il ramo `is_root_source`. Quindi
   `has_binding()` non discrimina "frame-bound / external GPU asset" da
   "static-compatible", come invece dichiara il commento del gate
   ("batching static-compatible layers is still allowed below"): nessuna
   sorgente immagine può essere static-compatible, quindi nessuna immagine può
   più finire in un batch. Le sorgenti di testo invece sì (batch
   `TextRun + Transform`), come pinna
   `test_frame_graph_compiler_validation.cpp`.
4. `00be928ab` non ha aggiornato né i due test né il golden, e non ha aggiunto
   alcun test di regressione per il comportamento che il gate dichiara di
   correggere (le sue modifiche di test sono solo vulkan-debug e video).

5. Il SIGSEGV nascondeva un **terzo** fallimento. La vecchia `CHECK` su
   `layer_batches[0]` faceva indicizzare un vettore vuoto e il binario moriva lì:
   doctest riportava `31 | 29 passed | 2 failed | 347 skipped`, cioè 347 casi
   mai eseguiti. Con la guardia `REQUIRE` la suite gira per intero:
   **378 casi, 375 pass, 3 failed**. Il terzo caso è
   `FrameGraphCompiler - TextRun bypasses Shape processor validation`
   (`test_frame_graph_compiler_validation.cpp:109,114`).
6. Il terzo fallimento ha **una causa diversa**, e separa i due commit.
   Ripetendo l'esperimento con la sola parte pre-`a78a8a29e` e girando la suite
   completa: **376 pass / 2 failed** → il caso TextRun **passa**, i due
   fallimenti immagine restano. Quindi:
   * `a78a8a29e` (`batch_is_sound`) causa solo il terzo caso: la sua clausola
     `members.size() < 2` sopprime il batch **degenerato di un solo membro**,
     e la fixture del test è un grafo con il solo nodo TextRun come output.
     Un batch di un membro non fonde nulla: la clausola è una policy
     ragionevole, quindi qui è l'**attesa del test** (proxy `lowered_into_batch`
     per il vero intento "TextRun non passa dal ShapeProcessor") a essere
     superata. La parte di `batch_is_sound` che conta — la contenzione dei
     consumer dentro l'isola — resta invece corretta e copre il bug reale
     documentato nel suo commento.
   * `00be928ab` (`frame_bound_image_source`) causa i fallimenti #1 e #2 e
     **non** tocca il caso TextRun.

Decisione: **il lowering è il difetto nel caso #1/#2** — il gate è troppo
ampio, non implementa il proprio intento dichiarato e ha invalidato un golden
certificato senza rigenerarlo: il test e il golden hanno ragione. Per il caso
#3 il lowering è *nuovo ma difendibile* (niente batch degenerati) e l'attesa
del test è superata: va aggiornato il proxy, non il gate.

Correzione applicata in questo perimetro: `CHECK` → `REQUIRE` sull'indice di
`layer_batches[0]` in `test_frame_graph_compiler_runtime.cpp`. Non è cosmetica:
ha fatto emergere il terzo fallimento latente e ha riportato la suite da 31 casi
eseguiti a 378.

Decisione applicata (opzione **B** — immagini native-only; il fused image path
è ritirato) e caso #3 chiuso aggiornando il proxy del test:

| Artefatto | Modifica |
|---|---|
| `frame_graph_builder_program_detail.hpp` | commento del gate riscritto: dichiara che l'esclusione è **totale** (non un sottoinsieme "frame-bound") e che non esiste più un layer immagine static-compatible; variabile rinominata `image_source_uses_native_path` perché `frame_bound_image_source` non descriveva ció che fa. **Nessun cambio di comportamento** (il golden rigenerato lo prova) |
| `test_frame_graph_compiler_runtime.cpp` | invariante riscritta: `100 Image layers stay on the native path and are never fused` → batch list **vuota**, i 100 layer immagine coperti da operations standalone, nessuna `lowered_into_batch`, output composite ancora eseguito, nessuna op fusa. `CHECK` → `REQUIRE` sull'indice (era SIGSEGV) |
| `frame_graph_program.golden` | rigenerato: `0x610ce49755184814` → `0x65304b41dbf40d8f`, con il motivo del cambio e il valore precedente annotati nel file |
| `test_frame_graph_compiler_validation.cpp` | proxy TextRun sostituito con l'intento reale: il node compila con `ShapeType::None` (bypass ShapeProcessor), possiede ≥1 operation standalone, non è fuso e non compare in nessun batch |

Risultato: `chronon3d_render_graph_tests` **378/378 PASS** (18653 assertion),
contro 31 casi eseguiti / 2 rossi prima.

## Demolition Debt — fused storage-image batch (ritirato)

| Campo | Valore |
|---|---|
| **Owner** | render graph / compiler lane (con la native image path lane) |
| **Reason** | Il fused storage-image batch non ha un contratto descriptor/readback equivalente per gli asset immagine: le immagini sono state spostate su `SourceNode` native path e `is_fusible_layer_node()` le esclude tutte. Restano in piedi i meccanismi del percorso ritirato senza produttore nel compiler, e vanno rimossi solo dopo la certificazione del sostituto. |
| **Exit condition** | native image path certificato: parità pixel e zero layer persi sul corpus overlay (incluso lo scenario `background + foreground + watermark` che ha motivato i due band-aid), più una run video reale. |
| **Equivalence test/gate** | confronto pixel native-path vs baseline certificata sul corpus overlay; `chronon3d_compositor_tests` + `chronon3d_render_graph_tests` verdi. |
| **Removal scope** | ramo immagine di `lower_layer_batches`, percorsi immagine di `node_runner_fused_batch.cpp`, istanze immagine in `CompiledLayerInstance`, `lowered_into_batch` per nodi Source, e i residui di `batch_is_sound` che esistono solo per i batch immagine. |
| **Status** | `ACTIVE` |

## Evidenza di build

```text
linux-video-fast-dev   (Vulkan + CUDA + native FFmpeg + text + daemon)
  cmake --build ... --target chronon3d_cli -j 12   → EXIT 0, 0 errori

linux-fast-dev         (tests ON)
  chronon3d_cache_tests                         146/146 case, 997574 assertion  PASS
  chronon3d_daemon_render_concurrency_tests      13/13 case,     94 assertion  PASS
  chronon3d_mtsdf_font_identity_tests             5/5  case,     36 assertion  PASS
  chronon3d_mtsdf_generator_tests                 7/7  case,    178 assertion  PASS
  chronon3d_backend_registry_tests               19/19 case,    108 assertion  PASS
  chronon3d_compositor_tests                     69/69 case, 613890 assertion  PASS
  chronon3d_render_job_contract_tests            14/14 case,    287 assertion  PASS
  chronon3d_render_graph_tests                  378/378 case, 18653 assert   PASS
    (prima: 31 casi eseguiti, 2 rossi — il SIGSEGV su layer_batches[0] vuoto troncava la suite)

gates
  python3.11 tools/check_architecture.py        GATE_PASS 191/191
  tools/check_test_hygiene.sh                   HYGIENE_PASS 3/3
  tools/check_filename_drift.sh                 nessun drift nuovo sui file toccati
```

Nota: il build video è una verifica di **compilazione**; nessun job video è stato
eseguito su GPU in questa sessione.

## Verifica (2026-09-16) — lane ricreata, suite eseguite, zero-copy certificato

Host: RTX A4000 16 GB, driver 595.91.07, 24 core, ccache 4.5.1, `mold` (auto-rilevato
in `CMakeLists.txt`), `ninja 1.13.0.git.kitware`. Build sempre con
`ninja -C build/chronon/<lane> -j12` (mai oltre `-j12`: 29 GiB di RAM condivisi con
altri servizi, il `-j18` ha prodotto swap thrash) e `CCACHE_DIR=$PWD/.ccache`.

### Lane e stato di build

```text
build/chronon/linux-video-release   (Release, Vulkan+CUDA+FFmpeg+text+video)
  ninja -j18 chronon3d_cli                        EXIT 0
build/chronon/linux-video-test      (ri-creata: .tmp/chronon-builds/linux-video-test)
  chronon3d_tests_video + chronon3d_cli_tests + 10 target di test   EXIT 0
```

Due difetti di **build** (non di codice) sono stati rimossi perché rendevano
impossibile ogni verifica incrementale:

1. `.ninja_deps` corrotto in ogni albero: ogni invocazione di `ninja` ri-eseguiva
   tutte le edge (`premature end of file; recovering` + `stored deps info out of
   date`). Rimedio e misure in
   [`../CCACHE-AUDIT-2026-09-10.md`](../CCACHE-AUDIT-2026-09-10.md).
2. Symlink pendente `build/chronon/linux-video-fast-dev` (albero liberato): i tool
   GPU lo cercano come candidato CLI. Rimosso; `linux-video-test` è ora il primo
   candidato della discovery e i tool accettano `CHRONON3D_CLI=<path>`.

### Suite eseguite dalla **radice del repo** (lane `linux-video-test`)

```text
chronon3d_render_graph_tests                378/378  PASS
chronon3d_cache_tests                       158/158  PASS (997596 assertion)
chronon3d_compositor_tests                   69/69   PASS
chronon3d_media_video_tests                 137/137  PASS
chronon3d_daemon_render_concurrency_tests    18/18   PASS
chronon3d_render_job_contract_tests          15/15   PASS (dopo il fix sotto)
chronon3d_mtsdf_generator_tests               7/7    PASS
chronon3d_mtsdf_font_identity_tests           5/5    PASS
chronon3d_native_video_sink_factory_tests     8/8    PASS
chronon3d_backend_registry_tests             59/59   PASS (era 50/59, vedi sotto)
chronon3d_cli_tests                         197/197  PASS (3690/3690 assertion,
   inclusi i casi GOP P0-5A/5B/5C/5D/5E/5F e il nuovo test_copy_lane_coverage)
chronon3d_native_decoder_tests                3/4     (1 rosso: heap corruption, sotto)
chronon3d_text_tile_raster_canary_tests      78/78   PASS (era 65/78: binario stantio,
   non un difetto GPU — vedi docs/evidence/text-tile-canary-2026-09-16.md)
```

### Due fix atterrati in questa sessione

1. **Vocabolario chiuso anche nello scope annidato** (`apps/chronon3d_cli/daemon/
daemon_service_ipc.cpp`). `reject_unknown_render_job_keys` validava le chiavi
   delle entry di `changed_intervals` **solo** sulla radice del payload, mentre il
   decoder legge l'array **anche** da `output_spec.changed_intervals`: un typo
   annidato (`start_secnds`) restava un default silenzioso, cioè esattamente il
   difetto che il gate esiste per rifiutare. Ora la validazione è una sola
   lambda applicata a entrambi gli scope. `chronon3d_render_job_contract_tests`:
   `decode_render_job_payload refuses a key outside the closed vocabulary` ROSSO →
   VERDE (15/15, 394/394 assertion).
2. **Harness del canary registry** (`tests/registry/test_backend_registry_vulkan_text_run.cpp`).
   Il caso `cached text-run builds glyphs on both styled cache miss and hit via
   Vulkan` costruiva il `RenderGraphContext` a mano senza cablare
   `ctx.gpu_styled_glyph_cache`: in produzione lo fa l'unica autorità di wiring
   (`src/render_graph/pipeline/scene_context_setup.cpp`), quindi la cache styled
   restava nulla, ogni frame risultava MISS e l'hit non poteva esistere
   (2 miss / 0 hit). Cablata dal `runtime`, come la produzione. Suite registry:
   **50/59 → 59/59** (847/847 assertion).

### Zero-copy certificato (host GPU reale)

`bash tools/verify_zero_copy_end_to_end.sh --frames 30` con la CLI della lane
release → **`ZERO_COPY_PASS` 16 PASS / 0 FAIL / 0 BLOCKED**:
`encoder_staging_copy_bytes=0`, `gpu_surface_copy_frames=0`,
`surface_handoff_path=direct`, `effective_backend=vulkan`,
`encoder_backend=nvenc`, `gpu_native_surface_frames=30`,
`gpu_native_encode_frames=30`. La richiesta `--direct-yuv` sulla scena builtin
procede invece con un downgrade **dichiarato e contato**
(`execution_reason=direct_yuv_capability_rejected`, `downgrade_count=1`,
`video_source_requested_frames=0`), quindi il certificato a conversione zero
richiede un piano con sorgente video. Numeri in
[`../ZERO_COPY_GATES.md`](../ZERO_COPY_GATES.md).

### Aperti emersi/nominati (non chiusi qui)

| Item | Stato | Causa / prossimo passo |
|---|---|---|
| `chronon3d_native_decoder_tests`: `NativeVideoFrameDecoder decodes independent sources concurrently` | **CHIUSO 2026-09-17 — ASan/UBSan PASS** | La causa era il buffer RGBA di staging per libswscale: la fixture Y4M 2×2 riceveva il solo `av_image_get_buffer_size`, senza il padding SIMD richiesto dalla scrittura allineata. Aggiunti 64 byte di padding, conservando `align=32`, in `native_video_frame_decoder_helpers.cpp`. Lane `linux-asan-native`, filtro `-tc='decoder*'`: **9/9 casi, 222/222 assertion, leak detection attiva, exit 0**. Evidenza: [`native-decoder-asan-2026-09-17`](../evidence/native-decoder-asan-2026-09-17.md) |
| `chronon3d_native_decoder_tests`: `CUDA-interop decode surface is Rgba32Float…` | **FIXED (harness)** | L'assertion si aspettava `LifetimeClass::FrameTransient`, ma l'unico consumatore di produzione di quel desc (`vulkan_cuda_frame_importer.cpp`) tiene un **ring di slot** di surface importate riusate tra frame e le rilascia solo invalidando lo slot → `JobPersistent`. Assertion allineata al contratto con la motivazione nel test |
| `chronon3d_text_tile_raster_canary_tests` | **FIXED 2026-09-16 — 78/78** | Il rosso era un **binario stantio**, non un contratto rotto: il `.ninja_log` della lane data l'ultima build del target alle 07:00 UTC, mentre l'ABI fix `5b59cfd48` atterra alle 16:33 UTC, e il target era escluso da ogni gate (quindi non ricostruito). Ricostruito dallo stesso albero: 78/78, 0 failed, 0 skipped su 3 run. Evidenza [`../evidence/text-tile-canary-2026-09-16.md`](../evidence/text-tile-canary-2026-09-16.md); gate GPU/WBH `tools/verify_text_tile_gpu_canary.sh` |
| Gate `steady_state_allocations` revision | **mismatch dichiarato** | `steady_state_allocations_per_frame=0.0` PASS, ma il report è pinnato a `revision=8bdf0f807` e `revision_state=mismatch` sull'albero corrente: il baseline va ri-pinnato alla revisione corrente quando la campagna Stage 1-5 chiude |
