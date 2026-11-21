# TICKET-PERF-RECEIPT-CONTRACT-FASTPATH — la verifica dell'artefatto non deve riaprire il file appena scritto

**Stato:** OPEN — 2026-11-20. Ticket di misura + intervento; nessuna modifica al codice in questa sessione.
**Area:** `src/media/video/output_contract.cpp`, `apps/chronon3d_cli/commands/video/exporters/pipe_export_finalize_encoder.cpp`, `src/verification/render_receipt.cpp`, `src/media/video/mux/packet_assembler_hash_writer.cpp`.
**Owner:** maintainer output contract / verification.
**Dipende da:** niente per la fase A. La fase B dipende dalla disponibilità dei fatti dell'encoder/muxer.
**Non sostituisce:** [`TICKET-PERF-100X-VERIFIED-PLAN`](TICKET-PERF-100X-VERIFIED-PLAN.md); non è il gate della receipt (`fast`/`normal`/`certify`), è il costo del contract check che oggi gira anche in `fast`.

---

## 0. Evidenza

`[CODICE]` `verify_output_contract` apre l'artefatto con `probe_media(...)` e poi calcola `sha256_file(...)`:

- `src/media/video/output_contract.cpp:116-140`: `probe_media(artifact)`; il tempo è registrato in `ffprobe_ms` (nome legacy: il probe è **in-process libavformat**, non un processo `ffprobe`).
- `src/media/video/output_contract.cpp:248-258`: `sha256_file(artifact)`.
- `apps/chronon3d_cli/commands/video/exporters/pipe_export_finalize_encoder.cpp:377-430`: il contract check gira dopo il mux, su ogni artefatto pubblicato, prima del rename finale.
- `apps/chronon3d_cli/commands/video/exporters/video_export_native_support.cpp:201-260`: `validation_ms`, `ffprobe_ms` e `sha256_ms` entrano nella timeline esclusiva del job.

`[MISURA]` Job reali del 2026-11-19:

| Job / item | `validation_ms` | `ffprobe_ms` | `sha256_ms` |
|---|---:|---:|---:|
| entity-rect-caption-oswald r2 / 0 | 68,1 | 58,1 | 10,0 |
| entity-rect-caption-oswald r2 / 1 | 77,7 | 66,6 | 11,0 |
| entity-rect-caption-abril r2 / 0 | 73,4 | 61,7 | 11,6 |
| entity-rect-caption-abril r2 / 1 | 77,6 | 63,1 | 14,4 |
| entity-rect-caption-oswald r3 / 0 | 62,5 | 54,1 | 8,4 |
| entity-rect-caption-oswald r3 / 1 | 88,5 | 74,8 | 13,6 |
| entity-chain-bg-oswald r21 / 0 | 46,4 | 33,3 | 13,0 |

Fonti: gli stessi `*-full.json` del ticket asset, blocco `overlay_render.items[*].artifact.chronon_telemetry.job`. Nei job caption la verifica vale 46–88 ms su un wall di 4,7–8,2 s (circa 1%); su un item corto da 300–500 ms diventa il 10–25%.

`[CODICE]` La receipt **riusa** già i fatti del contract quando disponibili (`src/verification/render_receipt.cpp:249-258`), quindi non c'è una seconda probe della receipt. Il costo residuo è la probe del contract stesso.

---

## 1. Obiettivo

Sul percorso `receipt_verify=fast`, l'artefatto è stato appena prodotto dal muxer di questo processo. Il contract check deve usare i fatti che il muxer/encoder conosce già — codec, width, height, pixel format, fps, frame count, durata, stream audio — e il digest incrementale quando disponibile, invece di riaprire il file con `avformat_open_input` + `avformat_find_stream_info`.

`normal` e `certify` continuano a fare la probe indipendente. Il percorso esterno (file non prodotto da questo processo, HDR mastering, segmenti non appena scritti) continua a fare la probe. Il default non cambia: `fast` resta il default, ma smette di pagare una riapertura che non aggiunge informazione sul file appena chiuso.

---

## 2. Piano (split)

### Fase A — Misurare il costo per campo e la provenienza

- Aggiungere al contract check `probe_source` (`in_process_mux` / `libavformat` / `fallback`) e `contract_fast_path` (bool).
- Misurare separatamente: apertura container, `find_stream_info`, lettura stream, SHA, verdict.
- Verificare quanti campi del contract sono già disponibili in `NativeAvEncoder`/`MuxSession` e quanti mancano (in particolare `frame_count` e `duration`).

**Gate A:** campi presenti in 3 run; il costo `ffprobe_ms` è attribuito; nessun campo del contract è inventato.

### Fase B — Fatti in-process per il path trusted

- Costruire un `OutputContractFacts` dal muxer/encoder: codec, width, height, pixel format, fps, frame count, durata video, stream audio, `output_published`.
- `verify_output_contract` accetta i fatti precompilati solo se: l'artefatto è regular file, non è HDR mastering, il muxer ha finalizzato il trailer, e il chiamante dichiara `trusted_in_process=true`.
- Se un campo manca o il file è stato modificato dopo la chiusura, fallback alla probe libavformat. Nessun "pass" per assenza di dato.
- Il frame count deve venire dal numero di frame inviati/encoded, non da una stima; la durata dal timebase del muxer.
- Il contract check continua a verificare truncation, audio parity e stream count. I test di artefatto troncato/audio mancante devono fallire come oggi.

**Gate B:** su 20 artefatti golden, tutti i campi della receipt sono identici al controllo; `ffprobe_ms` ≤ 5 ms; i test di troncamento/audio mancante restano FAIL; fallback testato.

### Fase C — Digest incrementale

- Per il path exporter, valutare `MuxOpenConfig::checksum` (oggi deliberatamente OFF, `include/chronon3d/media/video/packet_assembler.hpp:116-134`) o il digest già calcolato da `MuxAvioHashWriter`.
- MP4 classico fa backpatch del `mdat` e invalida l'hash append-only; non forzare un hash invalido. Usare il digest incrementale solo per output frammentato o quando il writer certifica `!checksum_used_reread`.
- In alternativa, mantenere `sha256_file` (8–15 ms) e chiudere la fase C come non conveniente.

**Gate C:** `sha256_ms` ≤ 2 ms sul path incrementale; digest identico al file; nessun re-read; `checksum_used_reread=0`; se non si può, la fase C resta `NOT RUN` e la fase B è comunque valida.

---

## 3. Rischi e vincoli

- **Non indebolire la verifica:** il fast path è ammesso solo per un artefatto appena prodotto e chiuso dallo stesso processo. Un file esterno o modificato usa la probe.
- **HDR:** il mastering metadata richiede la probe; escludere HDR dal fast path, come già fa la receipt.
- **Frame count:** il muxer può non avere un `nb_frames` finale; usare il contatore dell'encoder e verificare che coincida con i pacchetti. Se non coincide, fallback.
- **Audio:** il contract richiede stream audio e parity; i fatti in-process devono includere il numero di stream e la durata, non solo il video.
- **Segmenti:** i segmenti P4 hanno la stessa esigenza ma sono verificati uno per uno; il ticket parte dal path whole-timeline, poi valuta i segmenti.

---

## 4. Gate complessivo

- 3 run sullo stesso payload; confronto campo-per-campo della receipt con il percorso attuale.
- `validation_ms` p50 ≤ 20 ms (da 46–88 ms) e `ffprobe_ms` ≤ 5 ms; `sha256_ms` invariato o ≤ 2 ms se fase C.
- MP4 byte-identico; nessun falso PASS su artefatto troncato, audio mancante, stream count errato.
- Fallback alla probe quando un fatto manca; contatore `contract_fast_path` e `contract_probe_fallback`.
- `[NOT RUN]` implementazione e benchmark in questa sessione.

---

## 5. Demolition Debt

| Campo | Valore |
|---|---|
| **Owner** | maintainer output contract. |
| **Reason** | Il percorso in-process è un secondo modo di ottenere gli stessi fatti; la probe resta il fallback. |
| **Exit condition** | Il fast path copre tutti i job interni e il fallback è usato solo per file esterni/HDR; i campi `contract_fast_path` e `fallback` sono a 0/1 come atteso su 3 run. |
| **Equivalence test** | Receipt identica su 20 golden e MP4 byte-identico. |
| **Removal scope** | Il ramo `probe_media` nel path exporter fast, non la probe di `normal`/`certify`. |
| **Status** | `ACTIVE` durante la migrazione. |

Nessuna nuova cache: i fatti sono per-job e vengono passati al contract check.
