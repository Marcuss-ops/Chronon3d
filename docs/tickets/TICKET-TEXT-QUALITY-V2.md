# TICKET-TEXT-QUALITY-V2 — resa del testo: dove nasce davvero la perdita

Stato: **OPEN** (verifiche di codice: PASS; diagnostico di attribuzione: NOT RUN)
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
F9 (generatore) e F2 (colore). Vanno due campagne, non una.

## 3. Prerequisito di certificazione (bloccante)

`docs/FOLLOWUP_TICKETS.md:16` — **text tile raster canary OPEN (GPU)**:
`chronon3d_text_tile_raster_canary_tests` con 13 assertion di pixel
(`full.*`, `half.*`, `mtsdf.*`, `counts[*] >= 1`) a 0: il dispatch compute non
scrive nulla in quel canary. Finché è rosso, nessuna modifica di qualità del
testo può essere certificata pixel-level: **si rende verde prima di toccare lo
shader**.

## 4. Diagnostico di attribuzione (P0-0, da eseguire prima di ogni riga)

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

## 5. Piano ordinato

| Priorità | Lavoro | Gate |
|---|---|---|
| P0-0 | Diagnostico §4, esiti PASS/FAIL per leg + numeri salvati come evidenza | evidenza in `docs/evidence/` |
| P0-1 | Canary text tile verde (§3) | suite verde su GPU, 78/78 |
| P0-2 | Contratto colore: linearizzare via `math/color.hpp` **oppure** dichiarare sRGB nel contratto del text run | golden re-pinnato + test sul grigio medio (`#808080`) |
| P0-3 | Contratto encoder: rate control e preset espliciti per la lane overlay + bitrate minimo per contenuto grafico | artifact ≥ soglia, gate riproducibile |
| P1-1 | DirectYUV: fill a rampa al posto della soglia (lo stroke ha già la rampa) | equivalence test contro il bug watermark che aveva motivato la soglia |
| P1-2 | Sampling frazionario/bilineare delle letture atlas nei due lane | parità pixel + canary |
| P1-3 | Shadow: blur separabile float sul bbox del run, source-over, senza clip per-glifo; il 3×3 resta come `FastShadow` | delta frame time misurato + golden |
| P2-1 | Generatore: `msdfgen` come **leaf**, mantenendo `MtsdfProfile`/cache/preparation authority; `generation_id` 1→2 | byte-parity della preparation + re-pin del payload |
| P2-2 | Stroke con semantica miter = modalità vector separata (**ADR** obbligatorio) | ADR + parity gate |
| P2-3 | Policy Fast/High/Ultra come **estensione** della tassonomia `GlyphRepresentation` esistente (**ADR** + scheda Demolition Debt sul path compatibilità) | ADR approvato, nessun secondo resolver |

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
