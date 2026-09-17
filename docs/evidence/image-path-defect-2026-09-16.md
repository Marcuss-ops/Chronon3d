# Difetto della via immagine (PNG still / sequenza) — evidenza 2026-09-16

Evidenza di riferimento di
[`docs/tickets/TICKET-IMAGE-PATH-PIXEL-TRUTH.md`](../tickets/TICKET-IMAGE-PATH-PIXEL-TRUTH.md).
Scopo: fissare il profilo osservato e **separare ciò che è riproducibile in
albero da ciò che è una osservazione di sessione**, così che il caso non venga
riaperto come bug nuovo.

---

## 1. Profilo osservato

Su un plan reale 1920×1080 (lane Vulkan/FullGraph) renderizzato con output PNG:

| # | Osservazione | Valore |
|---|---|---|
| O1 | Contenuto composto | il frame **non contiene il testo** |
| O2 | Regione con contenuto | un **quadrante 960×540** (1/4 esatta dell'area 1920×1080); il resto uniforme |
| O3 | Variazione tra frame | PNG **byte-identici** a ogni indice di frame |
| O4 | Variazione tra backend | PNG **byte-identici** tra backend |
| O5 | Sanity scan | **accetta** (nessun NaN/Inf, vedi §3) |

**Provenienza:** O1–O4 sono l'osservazione di sessione che ha motivato il ticket;
non sono stati ri-misurati in questa chiusura perché la lane richiede un
workspace di job reale (`plan.json` + `prepared.json` + asset root), non presente
in questo checkout. O5 è una proprietà del **codice**, verificabile qui (§3), e
spiega perché il difetto non è auto-segnalante.

## 2. Parte riproducibile in albero (leg C)

La stessa famiglia di difetto sul percorso **still** è già misurata e in albero in
[`text-quality-attribution-2026-09-16.md`](text-quality-attribution-2026-09-16.md)
(leg C, esito **FAIL**):

| File | min / p50 / p99 / max (luma) | livelli distinti | pixel bianchi (>0.72) |
|---|---|---|---|
| `flat_still.png` (`render --start-frame 60 --end-frame 60 -o still.png`) | 0.498 / 0.498 / 0.498 / 0.498 | **1** | 0 |
| `flat_prod_f0.png` (estratto dallo stesso MP4) | 0.000 / 0.502 / 0.502 / 1.000 | 176 | 9 550 |

È la prova diretta di **O1** (testo assente) su quella via, con il controllo
positivo sullo stesso contenuto preso dall'MP4. Su un plan con sorgente video la
differenza è ancora più netta (`mean|d| = 177/255`, PSNR 2.64 dB).

## 3. Perché il sanity scan non lo vede (O5) — verificato nel codice

`write_frame_to_disk` (`apps/chronon3d_cli/utils/job/render_job_write_frame.cpp`)
chiama `scan_framebuffer_sanity(*fb)` e tratta `!ok` come reject duro. Il
contratto di quella funzione
(`apps/chronon3d_cli/utils/job/render_job_write_frame_sanity.hpp`) è:

```cpp
constexpr float kAlphaZeroRejectFraction = 0.85f;  // diagnostico, NON un reject
constexpr float kNanInfRejectFraction    = 0.05f;  // unico reject reale
constexpr int   kSanityScanStride        = 4;
```

e `tests/cli/test_render_job_write_frame_sanity.cpp` lo pinna esattamente così:
un framebuffer **interamente trasparente** è **accettato** (`ok == true`,
`alpha_zero_pixels == sampled_pixels`) perché un canvas trasparente è un
risultato legittimo per un overlay; **solo** NaN/Inf sopra il 5 % è rifiutato.

Conseguenza: un frame uniforme, duplicato e senza testo è **per costruzione**
accettato. Uno scan a stride 4 su un canvas 960×540-contenuto/resto-uniforme
campiona pixel validi e non ha alcun criterio per dire "manca il testo".
Aggiungere un frame-identity check **non** è la correzione: overlay statici
legittimi producono frame identici.

## 4. Il sostituto già atterrato

Commit `2a5579985` espone il **raw sink** pre-encode (`--video-sink raw`,
`VideoSinkType::RawFile`): frame host RGBA scritti senza encoder, selezione della
lane video dall'output intent, verifica aritmetica `frames × frame_bytes`.
Verifica registrata nel commit: **91 frame RGBA 1920×1080 = 754 790 400 byte**,
testo presente (0.84 % di inchiostro), **variabile tra i frame**.

Il raw sink copre quindi la verità pre-encode *della lane video*; la via
still/PNG resta il gap dichiarato in §5 del ticket.

## 5. Azione richiesta

Nessuna modifica di codice in questa chiusura. Il ticket
`TICKET-IMAGE-PATH-PIXEL-TRUTH` è il posto in cui la questione va risolta o
dichiarata non-canonica; il suo exit condition è in §5 della scheda.
