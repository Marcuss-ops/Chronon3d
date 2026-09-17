# TICKET-IMAGE-PATH-PIXEL-TRUTH — la via immagine non è una verità pixel

Stato: **OPEN (gap di verità pre-encode)** — **non** è un blocker di build né di
render: i job di produzione sono video, non PNG. La via immagine resta un
**surfacing di misura** rotto; il suo sostituto è atterrato ed è il **raw sink**
pre-encode.

Questo ticket esiste per **non riaprire il caso**: chiunque oggi lanci
`chronon3d_cli render … -o x.png` e trovi un frame senza testo sta osservando
questo difetto, non un nuovo bug.

---

## 1. Il difetto

La via immagine (`render -o *.png`, still a frame singolo e sequenza PNG) non
produce una verità pixel affidabile del renderer:

| Sintomo | Osservato |
|---|---|
| Testo composto | **Assente** — il frame non contiene i layer di testo |
| Contenuto | Confinato a **un quadrante 960×540** di un canvas 1920×1080 (esattamente 1/4 dell'area), il resto uniforme |
| Variazione tra frame | Byte **identici** a ogni indice di frame |
| Variazione tra backend | Byte **identici** a ogni backend |
| Sanity scan | **Non lo vede** (vedi §2) |

Misura già in albero che lo conferma sul percorso still, in
[`docs/evidence/text-quality-attribution-2026-09-16.md`](../evidence/text-quality-attribution-2026-09-16.md)
(leg C): `render --start-frame 60 --end-frame 60 -o flat_still.png` produce un
frame **uniforme** (`uniq=1`, luma 0.498, 0 pixel bianchi) mentre il PNG estratto
dallo stesso MP4 ha 176 livelli distinti e 9 550 pixel bianchi.

Profilo completo del difetto (i quattro sintomi sopra) in
[`docs/evidence/image-path-defect-2026-09-16.md`](../evidence/image-path-defect-2026-09-16.md).

## 2. Perché il sanity scan non può vederlo

`apps/chronon3d_cli/utils/job/render_job_write_frame.cpp::write_frame_to_disk`
esegue `scan_framebuffer_sanity(*fb)` prima di scrivere, ma il contratto di quel
controllo è **NaN/Inf only**:

* `render_job_write_frame_sanity.hpp` rifiuta solo quando la frazione NaN/Inf
  supera `kNanInfRejectFraction = 0.05f`;
* l'alpha-zero è **diagnostico**, non un reject: un canvas trasparente è
  legittimo per un render di overlay (`TICKET-RENDER-PIPELINE-INTEGRITY` ha
  rimosso il vecchio reject `kAlphaZeroRejectFraction = 0.60 → 0.85` proprio per
  non falsificare le animazioni fade-in).

Quindi **un frame uniforme, duplicato e privo di testo passa il gate per
costruzione**. Lo scan prova che il framebuffer è un framebuffer *valido*, non
che contiene ciò che il plan dichiara. Un frame-identity check sarebbe esso
stesso sbagliato come gate qui: overlay statici legittimi hanno frame identici.

## 3. Il sostituto: raw sink pre-encode

Commit `2a5579985` (*"feat(cli): expose the pre-encode raw sink as the
pixel-truth output path"*):

* `VideoSinkType::RawFile` (`--video-sink raw`, vocabolario unico in
  `apps/chronon3d_cli/commands/video/common/sink_options.hpp`) esisteva ma era
  **irraggiungibile**: nessun flag CLI, `sink_type` hardcoded a `Ffmpeg`, e
  `validate_video_job` rifiutava ogni valore non-ffmpeg nominando una modalità
  inesistente;
* ora è cablato end-to-end su **una sola** autorità di vocabolario condivisa da
  option checker, job validator e parser; la lane video è selezionata
  dall'**output intent** (così un `.raw` nudo non ricade nell'image writer);
* conserva il **formato host (rgba)** invece del default `yuv420p` dell'encoder:
  una conversione con chroma subsampling farebbe misurare il convertitore, non
  il renderer;
* verifica l'artifact **aritmeticamente** (`frames × frame_bytes`) invece di
  interrogare un container che non esiste (che cancellava un frame stream valido
  come "0 video streams");
* il sink viaggia su request/output spec canonico ed entra nel fingerprint
  CLI↔IPC come `out_sink`, così i due trasporti risolvono lo stesso job.

Verifica registrata nel commit: **91 frame RGBA pre-encode a 1920×1080 =
754 790 400 byte** (`91 × 8 294 400`), **testo presente** (0.84 % di inchiostro)
e **variabile tra i frame**.

## 4. Conseguenza sul programma qualità testo

* la verità pre-encode **esiste** ora per la lane video (raw sink);
* la verità pre-encode **non esiste** ancora per la via still/PNG, che è ciò che
  rende P0-0b di [`TICKET-TEXT-QUALITY-V2`](TICKET-TEXT-QUALITY-V2.md) ancora
  aperto: quel ticket chiede che `render -o x.png` componga il testo **oppure**
  che il PNG fedele sia prodotto via chunk/raw. Il raw sink copre la seconda
  metà per la lane video; la via immagine resta da sistemare o da dichiarare
  esplicitamente non-canonica.

## 5. Exit condition

Il ticket si chiude quando **una** delle due è vera:

1. la via immagine compone il testo e produce frame che variano con l'indice
   (parità pixel contro il frame decodificato dall'MP4 sulla stessa lane), con un
   test che fallisce se due frame di un plan non-statico sono byte-identici; **oppure**
2. la via immagine è dichiarata **non-canonica** per la verità pixel (messaggio
   esplicito nel CLI + documentazione), e ogni consumatore di qualità usa il raw
   sink o il frame decodificato.

Non si chiude "migliorando" il sanity scan: allargarlo a un frame-identity check
rifiuterebbe overlay statici legittimi.

## 6. Riferimenti

* evidenza: [`docs/evidence/image-path-defect-2026-09-16.md`](../evidence/image-path-defect-2026-09-16.md)
* attribuzione qualità testo: [`docs/evidence/text-quality-attribution-2026-09-16.md`](../evidence/text-quality-attribution-2026-09-16.md)
* ticket consumatore: [`TICKET-TEXT-QUALITY-V2`](TICKET-TEXT-QUALITY-V2.md) §P0-0b
* sanity scan: `apps/chronon3d_cli/utils/job/render_job_write_frame_sanity.hpp`,
  `tests/cli/test_render_job_write_frame_sanity.cpp`
* raw sink: `apps/chronon3d_cli/commands/video/common/sink_options.hpp`,
  `apps/chronon3d_cli/utils/video/video_sink_adapter_config.cpp`
