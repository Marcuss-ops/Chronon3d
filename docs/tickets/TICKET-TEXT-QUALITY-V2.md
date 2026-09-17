# TICKET-TEXT-QUALITY-V2 — resa del testo: dove nasce davvero la perdita

Stato: **OPEN — canary text-tile CHIUSO** (78/78, 2026-09-16: era un binario
stantio, vedi §3; il gate GPU è ora `tools/verify_text_tile_gpu_canary.sh`).
Verifiche di codice: PASS; diagnostico di attribuzione P0-0: **ESEGUITO**, esito
in §4.1. **P0-0b (verità pre-encode): SBLOCCATA 2026-09-17** — il PNG fedele è
una proiezione del raw sink (`tools/raw_frame_to_png.py`, evidenza
[`docs/evidence/pre-encode-pixel-truth-2026-09-17.md`](../evidence/pre-encode-pixel-truth-2026-09-17.md));
resta da eseguirla su un artifact di un job reale prima di dichiarare qualunque
guadagno pixel-level. Prossimo lavoro reale: **P0-2 contratto colore**.
Ambito: `text_tile_raster.comp` (FullGraph), `cuda_nv12_kernels.hpp` (DirectYUV),
`mtsdf_generator.cpp` (payload glifo), contratto colore del text run, contratto encoder.

Questo ticket esiste perché tre analisi esterne hanno indicato tre "colpevoli"
diversi. La verifica nel codice mostra che **hanno ragione su lane diverse**: le
correzioni non sono intercambiabili e vanno separate.

---

## 1. Fatti verificati (file:riga, non teoria)

| # | Fatto | Esito | Evidenza |
|---|---|---|---|
| F1 | FullGraph compone in **linear premultiplied** su `RGBA32F` | PASS | `src/backends/vulkan/shaders/text_tile_raster.comp:5,9`, source-over `:347-349` |
| F2 | I colori `#RRGGBB` entrano **sRGB-encoded** e non vengono linearizzati, pur finendo in un tile dichiarato linear | PASS (difetto di contratto) | `include/chronon3d/render_plan/color_utils.hpp:25-45` (`/255`); authority esistente e **non chiamata** su questo path: `include/chronon3d/math/color.hpp:121` |
| F3 | DirectYUV ha fill **binario** (lo stroke no) | PASS | `src/media/video/compositor/cuda/cuda_nv12_kernels.hpp:426`, rampa stroke `:437` |
| F4 | Atlas campionato **nearest** in entrambi i lane | PASS | `cuda_nv12_kernels.hpp:270`; `text_tile_raster.comp:249-253` |
| F5 | Shadow DirectYUV: campo distanza + binomiale 3×3 con tap a metà raggio | PASS | `cuda_nv12_kernels.hpp:290-310`; dichiarato in `docs/evidence/direct-yuv-text-shadow-2026-09-15.md` |
| F6 | Shadow Vulkan: soglia + `smoothstep` con cap `0.75*range`, clip al box atlas del **singolo glifo**, composizione `alpha-max` (non source-over) | PASS | `text_tile_raster.comp:305-320` |
| F7 | Fill usa `median3(r,g,b)`, stroke usa il canale **A**, ombra usa `median3` → tre derivazioni diverse dello stesso contorno | PASS | `text_tile_raster.comp:267,326` |
| F8 | Stroke = dilatazione del campo distanza ⇒ solo join tondi (miter richiede outline vettoriale) | PASS | `text_tile_raster.comp:326`; `cuda_nv12_kernels.hpp:437` |
| F9 | Generatore MTSDF custom: curve flatten, edge coloring `i % 3`; il commento CUDA ammette il fallimento della mediana RGB (Montserrat → watermark puntinato) | PASS | `src/backends/text/mtsdf_generator.cpp:251,297`; `cuda_nv12_kernels.hpp:409-414` |
| F10 | DirectYUV: il foreground è transfer-encoded e viene miscelato contro il background **encoded** (manca decode→linear→blend→encode) | PASS | `cuda_nv12_kernels.hpp:95`, transfer `:13-19,381` |
| F11 | HarfBuzz/hinting/subpixel **non** sono il problema: 26.6→float, `FT_LOAD_NO_BITMAP\|NO_HINTING` | FAIL (ipotesi scartata) | `font_engine_freetype.cpp:405,572`; `mtsdf_generator.cpp:251` |
| F12 | Encoding: artifact overlay a **535 kbps**, H.264 Main, `yuv420p`, tv-range (misurato con `ffprobe`); il daemon apre con `rc=driver-default, preset=p1` e `bit_rate` è settato solo in modalità `Bitrate` | PASS | `src/media/video/encode/native_av_sink.cpp:240-241`; log `[native_av] Opened native encoder` |

## 2. La matrix per lane (perché le tre analisi non si contraddicono)

| Lane | Chi la usa | Difetti che la riguardano |
|---|---|---|
| **FullGraph** (`RGBA32F`) | overlay senza sorgente video — i 100 overlay pubblicati (`video_decode_*_frames=0`, `composition_pipe=1`, `rgba_to_nv12_frames=120`) | F2, F4, F6, F7, F9, F12 |
| **DirectYUV** (`NV12`) | lane video con sorgente (NVDEC) | F2, F3, F4, F5, F7, F9, F10, F12 |

Conseguenza operativa: il fill binario (F3) **non** spiega gli overlay pubblicati;
li spiegano F12 (encode), F6/F7 (shadow e derivazione del contorno), F4 (nearest),
F9 (generatore) e F2 (colore). Vanno **due campagne separate, chiuse
separatamente**: una FullGraph (RGBA32F) e una DirectYUV (NV12). Una fix che vale
su una lane non è una fix per l'altra, e i 100 overlay di produzione sono tutti
FullGraph; la lane DirectYUV è selezionabile da altri plan, quindi i suoi difetti
restano reali ma **latenti** su questo host.

## 3. Prerequisito di certificazione — **CHIUSO (verde 78/78)**

`chronon3d_text_tile_raster_canary_tests` era registrato rosso a 13 assertion di
pixel (`full.*`, `half.*`, `mtsdf.*`, `counts[*] >= 1` a 0) e attribuito a "il
dispatch compute non scrive". **Il difetto non esiste nell'albero: era un
binario stantio.** Il `.ninja_log` della lane `linux-video-test` data l'ultima
build del target alle 07:00 UTC, mentre l'ABI fix `5b59cfd48` ("restore text tile
canary ABI contract") atterra alle 16:33 UTC; il target era escluso da ogni
gate, quindi nessuno lo ricostruiva dopo il fix. Ricostruito dallo stesso albero:
**78/78, 0 failed, 0 skipped, tre run identici**. Evidenza:
[`docs/evidence/text-tile-canary-2026-09-16.md`](../evidence/text-tile-canary-2026-09-16.md);
gate GPU/WBH: `tools/verify_text_tile_gpu_canary.sh`.

La suite resta **fuori** dalla lane headless `vulkan-gated-unit`: quella gira su
`ubuntu-latest` senza device, e il canary fa dispatch + readback su GPU reale, lì
darebbe uno skip silenzioso. La home corretta è il profilo WBH (host con GPU).

Conseguenza: **la certificazione pixel-level non è più bloccata dal canary**;
resta bloccata da **P0-0b** (la via still non compone il testo), che è un difetto
distinto e non dipende dallo shader.

## 4. Diagnostico di attribuzione (P0-0) — eseguito 2026-09-16

Flag già disponibili: `--frame`, `--rate-control {crf,qp,bitrate}`, `--crf`,
`--encode-preset`, `--gpu-hot-path-mode {require_gpu_native,require_direct_yuv}`,
`--keep-frames`, `--frames-dir`.

```bash
# stesso frame, quattro output
chronon3d_cli render --plan P --prepared-package K --frame 60 -o q/f060.png      # pre-encode truth
chronon3d_cli render --plan P --prepared-package K --frames 60 -o q/cur.mp4      # attuale
chronon3d_cli render ... --rate-control crf --crf 10 --encode-preset p5 -o q/hq.mp4
chronon3d_cli render ... --gpu-hot-path-mode require_direct_yuv -o q/dyuv.mp4
```

Letture: PSNR/SSIM decoded-vs-PNG (peso encode/chroma) e profilo del bordo
(larghezza rampa, picco) su PNG vs MP4. Attribuzione: PNG pulito + MP4 sporco →
encode/chroma; FullGraph pulito + DirectYUV no → compositing/color path; PNG
sporco → rasterizzatore. Shadow e stroke vanno ripetuti ON/OFF separatamente.

### 4.1 Esito (evidenza: [`docs/evidence/text-quality-attribution-2026-09-16.md`](../evidence/text-quality-attribution-2026-09-16.md))

| Leg | Esito | Numero |
|---|---|---|
| Lane usata dai job reali | PASS | **24/24 FullGraph, 0 DirectYUV** in 30 min; `require_direct_yuv` → `FAIL_CLOSED` (`requires_full_frame_rgba`) |
| Peso dell'encode su overlay | PASS | produzione vs HQ (`qp18/p5`) = **53.30 dB**, `mean|d| 0.034/255`, 16 pixel oltre 64/255 su 2 073 600 |
| Verità pre-encode (PNG still) | **FAIL** | il frame PNG è **uniforme** (`uniq=1`, luma 0.498): la via still **non compone il testo** → nessuna certificazione pixel-level possibile |
| VRAM shape overlay | PASS | **592 / 593 MiB** delta per job attivo, due run indipendenti (la calibrazione 8.8 GB non copre questa shape). **Aggiornato 2026-09-16:** il probe è stato **promosso** da strumento one-off a harness riproducibile — `cmd/vram-probe -mode two-runtime-pool` + evidenza [`docs/evidence/vram-two-runtime-pool-2026-09-16.md`](../evidence/vram-two-runtime-pool-2026-09-16.md) — con **due pool caldi posseduti** e fasi etichettate. Non è ancora un gate pass/fail: il passo successivo è la **telemetria interna degli allocator**, perché su host condiviso il contatore device-wide varia di oltre 3× a parità di attribuzione per processo |
| Budget daemon | PASS (parziale) | 1 280 ms per job non attribuiti da nessun timer; receipt 284 ms + 253 ms (verifica doppia); `CHRONON_RENDER_JOB_SLOTS=2` è no-op |

Conseguenze sul piano, in ordine:

1. il primo blocco non è lo shader: è **la verità pre-encode** (leg C FAIL) — senza
   di essa ogni fix di qualità è non certificabile, ed è coerente con il canary
   text-tile rosso;
2. i fix **DirectYUV** (fill binario, ombra 3×3, blend in Y) scendono di priorità:
   su questo host sono **latenti** (0 job li usa); restano nella scheda come P1-DY
   perché la lane esiste ed è selezionabile da altri plan;
3. l'**encode non è la prima causa** su contenuto overlay: il contratto
   (`driver-default`) va comunque dichiarato, ma non è il jackpot che sembrava;
4. la classifica dei sospetti si sposta su **shadow per-glifo + alpha-max + clip al
   box**, **sampling nearest**, **derivazione incoerente fill/stroke**,
   **generatore MTSDF custom** e **colore non linearizzato**.

## 5. Piano ordinato

| Priorità | Lavoro | Gate |
|---|---|---|
| ~~P0-0~~ | Diagnostico §4 — **eseguito** (evidenza in `docs/evidence/`) | PASS/FAIL registrati |
| P0-0b | **Verità pre-encode** — **SBLOCCATA 2026-09-17**: la via immagine è dichiarata non-canonica ([IMAGE-PATH-PIXEL-TRUTH](TICKET-IMAGE-PATH-PIXEL-TRUTH.md), exit condition 2) e il PNG fedele esiste come proiezione del raw sink (`tools/raw_frame_to_png.py`, [evidenza](../evidence/pre-encode-pixel-truth-2026-09-17.md)) | il tool **rifiuta** un frame uniforme (`content_fraction=0` ⇒ exit 1) e produce il PNG verbatim; resta da eseguirlo su un artifact di un job reale |
| ~~P0-1~~ | Canary text tile verde (§3) — **CHIUSO 2026-09-16** | suite verde su GPU, **78/78**, 3 run; gate `tools/verify_text_tile_gpu_canary.sh` |
| P0-2 | Contratto colore: linearizzare via `math/color.hpp` **oppure** dichiarare sRGB nel contratto del text run | golden re-pinnato + test sul grigio medio (`#808080`) |
| P0-3 | Contratto encoder: rate control e preset espliciti per la lane overlay + bitrate minimo per contenuto grafico | artifact ≥ soglia, gate riproducibile |
| P1-1 | DirectYUV: fill a rampa al posto della soglia (lo stroke ha già la rampa) | equivalence test contro il bug watermark che aveva motivato la soglia |
| P1-2 | Sampling frazionario/bilineare delle letture atlas nei due lane **+ larghezza di coverage screen-space**: il filtraggio da solo passa da nearest aliasato a bilineare un po' meno aliasato, non risolve la minificazione. Ogni sample va **clampato al rect del singolo glifo nell'atlas** con disciplina half-texel, altrimenti a bordo pagina si legge il glifo vicino | parità pixel + canary + test di bordo pagina |
| P1-3 | Shadow canonica **sul run**, non sul singolo glifo: silhouettes dei glifi → **union mask del TextRun** → spread opzionale → gaussiana separabile → colore/opacità → source-over **lineare**. Risolve insieme clipping per-glifo, `alpha-max` sugli overlap e blur non fisicamente coerente. Il 3×3 attuale resta come modalità **`Fast`** (non va eliminato: è il percorso a throughput massimo) | delta frame time misurato + golden |
| P2-1 | Generatore: `msdfgen` come **leaf**, mantenendo `MtsdfProfile`/cache/preparation authority; `generation_id` 1→2 | byte-parity della preparation + re-pin del payload |
| P2-2 | Due semantiche di stroke **dichiarate**, non un SDF "aggiustato": `DistanceStroke` (veloce, join round/offset — l'attuale dilatazione del campo distanza) e `VectorStroke` (miter/bevel/round + miter limit + representation HQ). Non si cerca di far sembrare un miter un SDF: non lo diventerà (**ADR** obbligatorio) | ADR + parity gate |
| P2-3 | Policy Fast/High/Ultra come **estensione** della tassonomia `GlyphRepresentation` esistente (`include/chronon3d/runtime/gpu_glyph_atlas.hpp`), che è il posto giusto per la decisione — **nessun secondo text renderer** (**ADR** + scheda Demolition Debt sul path compatibilità) | ADR approvato, nessun secondo resolver |

### 5.1 Ordine di esecuzione (vincolante)

1. **Diagnostico a 4 output** (P0-0, §4) — eseguito; il suo output **è il golden
   corpus** di questa campagna (PNG/raw pre-encode, FullGraph, DirectYUV, H.264
   HQ), non un esperimento una tantum;
2. ~~**Canary text-tile verde** (P0-1)~~ — **CHIUSO 2026-09-16 (78/78)**; il
   blocco che ne seguiva era **P0-0b** (verità pre-encode), ora **sbloccata**
   (2026-09-17): il campione pre-encode è il raw sink, proiettato in PNG dal
   tool canonico. Il passo successivo della catena è il **contratto colore** (3),
   che può ora essere misurato su quel campione:
3. **Contratto colore** risolto **alla boundary di authoring/lowering**, non
   dentro gli shader: se `#808080` significa CSS/sRGB, deve diventare linear **una
   volta sola** prima di arrivare ai backend (niente `to_linear()` separato in
   Vulkan e CUDA);
4. **Contratto encoder** (P0-3): 535 kbps può mascherare qualunque progresso del
   renderer;
5. **Sampling frazionario + coverage screen-space** (P1-2);
6. **Shadow canonica sul run/bbox** (P1-3);
7. **msdfgen / representation HQ / miter** (P2-1..P2-3), dopo che il percorso base
   è misurabile e stabile.

## 6. Vincoli di repo applicabili

- Nessun resolver/registry/cache nuovo senza **ADR** (AGENTS.md); la tassonomia
  delle rappresentazioni (`include/chronon3d/runtime/gpu_glyph_atlas.hpp`) e il
  profilo canonico (`include/chronon3d/text/mtsdf_profile.hpp`: `em 64`,
  `range 16`, `padding 24`) esistono già: l'estensione è una policy, non un
  secondo authority.
- DirectYUV resta la lane veloce: ogni fix di qualità deve dichiarare il costo
  (il render loop misurato è 260 ms per 120 frame 1080p, quindi il budget per
  una shadow sul bbox esiste).
- `--keep-frames`/`--frames-dir` rendono la verità pre-encode parte del
  prodotto: nessun harness nuovo per il diagnostico.
- Cronaca estesa solo qui; nei canonici al massimo una riga sintetica.
