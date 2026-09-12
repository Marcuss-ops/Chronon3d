# Codec Boundary Audit — `src/media/video` (2026-09-12)

> **Modalità:** read-only census. 0 modifiche al codice, 0 build.
> **Scope:** `src/media/video/**`, `include/chronon3d/media/video/**` e il lato
> export che li consuma (`apps/chronon3d_cli/commands/video/**`,
> `apps/chronon3d_cli/utils/video/**`).
> **Perché:** l'audit del 2026-09-09 ha **campionato** quest'area, non esaminata
> riga per riga. È il punto di partenza dichiarato per *Minimum Necessary
> Rendering* (PacketCopy, GOP dependency model, BoundaryReencode, Codec Decision
> Reuse). Le risposte sotto sono ancorate a file:riga reali.

---

## 0. Le 8 domande, con risposta

| # | Domanda | Risposta (authority) |
|---|---|---|
| 1 | Dove **nasce** il packet compresso? | due origini: NVENC (`src/media/video/encode/native_av_sink.cpp:457` `av_packet_move_ref`) e il copy-from-source (`src/media/video/mux/packet_assembler.cpp:368,395` `av_packet_ref`) |
| 2 | Dove viene **perso**? | nel decoder: read_packet → `avcodec_send_packet` (`native_video_frame_decoder_decode.cpp:364`) → `avcodec_receive_frame` (:287). Il packet compresso **non è mai trattenuto**: il decoder trattiene solo *frame decodificati* (window di 6, `native_video_frame_decoder.hpp:127`) |
| 3 | Quando siamo **obbligati a decodificare**? | ogni richiesta di sample non presente in window/cache ⇒ seek al keyframe precedente + re-decode del GOP (`decode.cpp:188` `previous_keyframe(...)` + `:190` `av_seek_frame(..., AVSEEK_FLAG_BACKWARD)`) |
| 4 | Dove possiamo **conservarlo**? | solo nel path mux/copy (`packet_assembler.cpp`, `assemble_segments`, flag `gop_copy_only` via `video_execution_resolver.cpp:95`). **Nessuna residenza di packet compressi nel render path** |
| 5 | Quale **metadata codec** conserviamo già? | `SourceSampleTable::SourceSample` = `{pts, duration, dts, source_order, continuity_id, keyframe}` (`source_sample_table.hpp:17-25`); `EncodedPacket = {packet, time_base, keyframe}`; `MediaProbeInfo` (codec, time_base, frame_rate, format) |
| 6 | Come gestiamo **PTS/DTS**? | `SourceSampleTable` è PTS-nativo ed esatto (mai FPS nominale; `finalize()` riempie le durate dai PTS vicini); l'autorità di rescale sul mux è **`MuxSession`** (`packet_assembler.cpp:544,556,568,609` `av_packet_rescale_ts`). DTS è registrato e propagato nei diagnostici ma non guida la selezione |
| 7 | Quale authority conosce **keyframe/GOP**? | `SourceSampleTable::previous_keyframe()` (per `continuity_id`) + `AV_PKT_FLAG_KEY` (`packet_assembler.cpp:355,376`, `helpers.cpp:119`). **Non esiste un modello di dipendenze di riferimento** oltre al flag keyframe |
| 8 | Dove avviene il **primo materialize-to-pixels**? | `src/media/video/native_video_frame_decoder_decode.cpp:253` `av_hwframe_transfer_data` (GPU→CPU, lane RGBA interop) e `src/media/video/native_video_frame_decoder_helpers.cpp:46` `sws_scale` (fallback CPU). Il commento in `native_video_frame_decoder.hpp:56` fissa il contratto: decodifica nativa ⇒ view RGBA float4, **mai** una surface di encode YUV |

---

## 1. Mappa del boundary

```
                 ┌──────────────────────────── MUX (authority timestamp) ───────────────────────────┐
                 │  MuxSession.submit_video / submit_audio → av_packet_rescale_ts (packet_assembler.cpp:544+)
                 └──────────────────────────────────────────────────────────────────────────────────┘
   NVENC ──receive_packet──► av_packet_move_ref ──► EncodedPacket{keyframe} ──► MuxSession
   (native_av_sink.cpp:450-458)

   source container ──demux──► AVPacket ──av_packet_ref──► own copy ──► MuxSession
   (packet_assembler.cpp:355-395, GOP boundary: solo il primo input può iniziare non-key)

   RENDER PATH (decode, lossy per il codec):
   source ──demux──► SourceSampleTable (PTS/keyframe) ──► seek a previous_keyframe ──►
        avcodec_send_packet ──► avcodec_receive_frame ──► [GPU] av_hwframe_transfer_data
                                                          [CPU] sws_scale
        ──► Framebuffer (RGBA) ──► retained window (6) / framebuffer cache (64)
```

---

## 2. Finding per l'audit

### CB-1 (P0 strutturale) — il packet compresso è usa-e-getta nel render path
`NativeVideoFrameDecoder::Session` conserva una window di **6 frame decodificati**
(`native_video_frame_decoder.hpp:120-127`, `retained_frames`) e una cache
framebuffer di 64 (`LruCache<int64_t, Framebuffer>`), ma **zero packet compressi**.
Ogni sample non in window/cache riparte dal keyframe precedente e ri-decodifica
l'intero GOP (`decode.cpp:188-190`). Implicazione per PacketCopy/GOP Surgery:
oggi il codec è materializzato per la prima cosa che serve *pixel*, e il
bitstream intermedio non è un'entità di prima classe.

### CB-2 (P0) — nessun modello di dipendenza di GOP
L'unica conoscenza di GOP è il bit `keyframe` per sample e
`previous_keyframe()`. Non c'è traccia di:
lista di riferimento, POC/`coded_picture_number`, dipendenza tra P-frame,
o `AV_FRAME_FLAG_*` propagati. `BoundaryReencode` esiste come valore in
`AudioExecutionPath` (`packet_assembler.hpp:47`) e come intent nel resolver
(`video_execution_resolver.cpp:281`), ma non esiste un boundary
*reencode* che consumi un modello di dipendenze — coerente con lo stato
"prototype non ancora iniziato".

### CB-3 (P1) — DTS trasportato ma non usato per l'ordine
`SourceSample.dts` è popolato (`helpers.cpp:116`) e propagato in ogni
`DecodeDiagnostic`, ma la selezione usa solo `pts`+`source_order`
(`source_sample_table.hpp`): un flusso con reordering B-frame non usa il DTS
per ricostruire l'ordine di decodifica. Da confermare su un corpus con B-frame;
se emerge, `source_order` (ordine di demux) è già il tie-break stabile su cui
appoggiarsi.

### CB-4 (P1) — il primo materialize-to-pixels è già nel decoder, non nel renderer
Il passaggio NV12/P010 → RGBA avviene dentro `NativeVideoFrameDecoder`
(`decode.cpp:253` transfer, `helpers.cpp:46` sws_scale). Questo è il punto in cui
"Minimum Necessary Rendering" paga il costo oggi: qualunque decisione di
conservazione del packet deve **spostare** il materialize fuori dal decoder o
renderlo condizionale, altrimenti il packet compresso non serve a nulla.

### CB-5 (P2) — due authority di copia bitstream
Il copy path esiste in due forme: `MuxSession`/`PacketAssembler`
(`packet_assembler.cpp`, con `submit_copied_video`) e `assemble_segments`.
Entrambe usano `av_packet_ref` + rescale sul mux. Non ridondanza di
*conoscenza* (il timestamp ha un'autorità unica: `MuxSession`), ma due punti di
ingresso da certificare quando PacketCopy diventa una modalità di prima classe.

### CB-6 (P2) — `AudioExecutionPath` come precedente di design
`resolve_audio_execution(content_changed, boundary_trim, codec_compatible)`
(`packet_assembler.hpp:41-58`) è già la forma "*decidi prima, esegui senza
branch*" che serve al video: `CopyPackets → TrimPackets → BoundaryReencode →
FullReencode`. Il video dovrebbe adottare la stessa firma di decisione, ma
giorno non ha la nozione di "content_changed" né di "codec_compatible" per il
bitstream.

> **Stato al 2026-09-12 (sessione successiva): prototipo realizzato.** Vedi
> §5. Il video ora parla la stessa lingua di decisione dell'audio.

---

## 3. Domande aperte → esperimenti proposti (in ordine)

1. **PacketCopy feasibility**: quanto costerebbe trattenere i packet compressi
   (memoria per secondo, a parità di output) invece di decodificare?
   Misurare: byte/s del bitstream, #seek+re-decode per export, `retained_replays`.
2. **GOP dependency model**: servono reference list / POC per decidere un
   boundary di reencode? Partire da un corpus con B-frame e verificare se
   `previous_keyframe()` è sufficiente o se serve un grafo.
3. **BoundaryReencode prototype**: intorno a `AudioExecutionPath`, definire
   `VideoExecutionPath` con gli stessi 4 stati e un resolver
   backend-neutro gemello di `video_execution_resolver`. — **fatto: vedi §5
   (il resolver esiste; l'*esecuzione* BoundaryReencode resta da scrivere).**
4. **Codec Decision Reuse**: la decisione (`CopyPackets`/`BoundaryReencode`)
   è per-layer o per-job? Oggi `gop_copy_only` è un flag di job
   (`command_render_plan.hpp:30`); un modello per-layer richiederebbe di
   spostare la decisione in preparazione, prima del loop.

## 4. Cosa NON è stato fatto

- Nessuna modifica al codice: l'audit è una census.
- Nessuna misura runtime: i numeri richiedono il corpus ufficiale e un host con
  GPU/NVENC; l'audit identifica le authority da strumentare.
- Nessuna lettura di `packet_assembler.cpp` oltre le righe citate: il file
  (633 righe) e `native_video_frame_decoder_decode.cpp` (575 righe) meritano
  una lettura integrale in una sessione dedicata prima di toccare il boundary.

---

## 5. Prototipo `VideoExecutionPath` (2026-09-12)

Realizzato il passo 3 del §3: il codec boundary video ora ha un vocabolario e
un'autorità di decisione **gemelli** di quelli audio, così i due confini non
parlano più due lingue diverse.

| Artefatto | Dove |
|---|---|
| `enum class VideoExecutionPath { CopyPackets, TrimPackets, BoundaryReencode, FullReencode }` | `include/chronon3d/media/video/video_execution_resolver.hpp` |
| `resolve_video_execution_path(content_changed, boundary_change, codec_compatible, splice_safe)` — tabella pura `constexpr`, nessuna capability | idem |
| `resolve_video_execution_path_from_gop_analysis(intent_gop_copy_only, intent_allow_hybrid_gop, analysis)` — ponte dalle fact di preflight | idem |
| `VideoExecutionDecision::codec_path` — la decisione riportata col piano | idem |
| Implementazione + autorità unica nei due entry point | `src/media/video/video_execution_resolver.cpp` |
| Test (tabella, fail-closed, coerenza col piano) | `tests/cli/test_packet_assembler_contract.cpp` |

### Perché `splice_safe` e la firma a 4 argomenti
`resolve_audio_execution` ha 3 fatti. Il video ne ha uno in più, e non è una
capability: la concatenazione di packet copiati + packet NVENC può essere
indecodibile anche quando il cambio è confinato al boundary. Quindi la tabella
degrada `BoundaryReencode → FullReencode` quando `splice_safe == false`.

### Fail-closed
Il ponte non inventa mai un copy: se la preflight non ha ispezionato la sorgente
(`analysis.available == false`), `codec_compatible` è falso e ogni intent
collassa a `FullReencode`. Un intent di copy esplicito che la preflight non può
dimostrare diventa `FullReencode`, non un copy non verificato.

### Autorità unica
`resolve_plan_from_capabilities` non scrive più a mano i valori di codec path:
le due branch di riuso delegano alla tabella pura. `resolve_video_execution_with_gop_analysis`
calcola una sola volta `boundary_path` dal ponte e lo assegna alle branch che
copiano davvero i packet; i downgrade riportano `FullReencode` (il piano
ri-encoda ogni frame) insieme a `downgrade_count` + `reason`. Un executor non
può quindi dichiarare un riuso che il piano non esegue.

### Cosa resta (non in questo prototipo)
- **Esecuzione** `BoundaryReencode`: il resolver la decide, ma non esiste ancora
  un boundary re-encoder che consumi un modello di dipendenze di GOP (CB-2).
- **`TrimPackets`**: raggiungibile dalla tabella, non ancora prodotto da alcun
  entry point (il trim richiede la mappatura time-range → GOP).
- **Per-layer vs per-job** (esperimento 4 del §3): non toccato.
- Nessuna misura runtime: il prototipo non cambia il data path, quindi non ha
  effetti misurabili sull'export.
