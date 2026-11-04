# TICKET-VIDEO-CODEC-BOUNDARY — the codec boundary gets one decision language

- **Stato**: chiuso (2026-11-02) — `BoundaryReencode` e decisione per-job verificati sul preset `linux-video-test`; scheda archiviata il 2026-11-04.

## Stato — chiuso (2026-11-02)

- [x] **`BoundaryReencode` (finding CB-2)** — executor GOP presente: usa una singola `MuxSession` per GOP copiati e ri-encodati, con schedule e timestamp sorgente conservati fino al piano (`apps/chronon3d_cli/utils/video/video_job_execute_gop.cpp`, `src/media/video/gop_execution_executor.cpp`). Il downgrade legacy `hybrid_pipeline_unavailable` non è presente.
- [x] **Decisione per-layer vs per-job (esperimento 4)** — confermato per-job: codec, changed intervals, stream timestamps, muxer e GOP execution plan appartengono alla singola esportazione; una decisione per-layer richiederebbe output/mux separati e un contratto non esistente. Se servirà una policy diversa per un layer, va espresso come job di export separato.

Verifica CPU-only locale: preset `linux-video-test`, `CHRONON3D_ENABLE_CUDA_INTEROP=OFF`, feature vcpkg `native-ffmpeg` (FFmpeg con libx264/libx265). `chronon3d_cli_tests`: 273 casi e 5.563 asserzioni passati. Gate mirati: P0-5B, 1 caso/1.295 asserzioni; P0-5D e P0-5E/5F, 2 casi/214 asserzioni. Nessun test GPU è stato eseguito. Le preset video installano ora esplicitamente `native-ffmpeg`; due callsite CLI obsoleti sono stati aggiornati alle firme correnti per compilare il target.

### Decisione per-job — 2026-10-29

`VideoExecutionDecision` resta al confine del job. Il resolver riceve le
capability del dispositivo e la schedule GOP dell'intera esportazione; non
esistono stream/muxer indipendenti per layer. Spostare ora la scelta nei layer
duplicherebbe la policy e non cambierebbe il piano realmente eseguito. La
decisione per-job e il gate `BoundaryReencode` sono chiusi.

## Contesto

> **Aggiornamento 2026-11-02:** il boundary executor e il corpus di parità
> sono verificati sul preset `linux-video-test` con FFmpeg nativo. Il preset
> abilita ora la feature `native-ffmpeg`, che fornisce gli encoder richiesti.

L'audio ha già un'autorità di decisione pura per il proprio confine di codec:

```cpp
// include/chronon3d/media/video/packet_assembler.hpp:39-51
constexpr AudioExecutionPath resolve_audio_execution(
    bool content_changed, bool boundary_trim, bool codec_compatible) noexcept;
// CopyPackets → TrimPackets → BoundaryReencode → FullReencode
```

Il video non aveva né il vocabolario né la tabella: la decisione di riuso del
bitstream era sparsa in due punti che la scrivevano a mano
(`src/media/video/video_execution_resolver.cpp`, branch `explicit_gop_copy` e
`hybrid_gop_source`), e `VideoExecutionDecision` non riportava affatto *come* il
bitstream veniva riusato. Una qualunque futura aggiunta di branch poteva quindi
introdurre un secondo parere sulla stessa domanda.

Questo ticket chiude i passi 3 e 4 del §3 di
[`docs/AUDIT-2026-09-12-VIDEO-CODEC-BOUNDARY.md`](../../AUDIT-2026-09-12-VIDEO-CODEC-BOUNDARY.md)
con l'executor boundary e la decisione per-job documentati sopra.

## Modifiche implementate

**File**: `include/chronon3d/media/video/video_execution_resolver.hpp`,
`src/media/video/video_execution_resolver.cpp`,
`apps/chronon3d_cli/utils/video/video_job_execute_dispatch.cpp`,
`tests/cli/test_packet_assembler_contract.cpp`

1. `enum class VideoExecutionPath { CopyPackets, TrimPackets, BoundaryReencode,
   FullReencode }` — gli stessi quattro stati dell'audio.
2. `constexpr resolve_video_execution_path(content_changed, boundary_change,
   codec_compatible, splice_safe)` — tabella pura, nessuna capability. Il quarto
   argomento è il fatto che solo il video ha: la concatenazione di packet copiati
   + packet NVENC può restare indecodibile anche quando il cambio è confinato al
   boundary, quindi degrada `BoundaryReencode → FullReencode`.
3. `resolve_video_execution_path_from_gop_analysis(intent_gop_copy_only,
   intent_allow_hybrid_gop, analysis)` — il ponte dalle fact di preflight.
   Fail-closed: `analysis.available == false` ⇒ `codec_compatible == false` ⇒
   ogni intent collassa a `FullReencode`; un intent di copy esplicito che la
   preflight non può dimostrare non diventa mai un copy non verificato.
4. `VideoExecutionDecision::codec_path` — la decisione riportata insieme al piano.
   Le due branch di `resolve_plan_from_capabilities` non scrivono più il valore a
   mano: delegano alla tabella pura. `resolve_video_execution_with_gop_analysis`
   calcola `boundary_path` una volta sola e lo assegna alle branch che copiano
   davvero i packet; i downgrade riportano `FullReencode` (il piano ri-encoda ogni
   frame) insieme a `downgrade_count` e `reason`. Un executor non può dichiarare
   un riuso che il piano non esegue.
5. Il dispatch CLI logga `codec={}` accanto a decode/composite/encode/interop/
   handoff, e `finalize_composition_execution` mantiene l'invariante
   (piano render-graph ⇒ `codec_path = FullReencode`).
6. Test in `tests/cli/test_packet_assembler_contract.cpp`: tabella pura, ponte
   fail-closed, e coerenza `codec_path` ↔ piano per intent, copy provato,
   downgrade e composizione.
7. `TrimPackets` è ora prodotto dal preflight quando la richiesta non modifica
   i pixel e il relativo intervallo contiene almeno un GOP completo copiabile.
   L'executor packet-copy ricerca i confini random-access, scarta i GOP
   parziali e riallinea audio/video alla finestra effettivamente copiata. Il
   `MinimumWorkSchedule` conserva anche PTS, DTS e time-base sorgente fino al
   `GopExecutionPlan`, così il futuro boundary executor non deve reinterpretare
   i tick del codec come frame della timeline.
8. Il contratto canonico trasporta `ChangedInterval` tipizzati (`content` o
   `trim`) da CLI/IPC al preflight. La CLI accetta l'opzione ripetibile
   `--changed-interval start:end[:content|trim]`; l'IPC usa
   `output_spec.changed_intervals`.

## Accettazione

| Gate | Eval |
|---|---|
| La tabella pura copre i 4 stati + i due degradi (`interior change`, `unsafe splice`) | unit test, `test_packet_assembler_contract.cpp` |
| Il ponte non produce mai un copy non provato | `availability=false` ⇒ `FullReencode` (unit test) |
| `codec_path` è coerente col piano finale | copy provato ⇒ `CopyPackets`; downgrade/composizione ⇒ `FullReencode` (unit test) |
| Nessun gate di correttezza esistente regredisce | suite `chronon3d_cli_tests` verde; arch gate verde |

## Follow-up fuori da questo ticket

- **Nessuna misura runtime**: il prototipo non cambia il data path, quindi non ha
  effetto misurabile sull'export.

## Demolition Debt

Il percorso legacy `hybrid_pipeline_unavailable` non è presente. L'executor
GOP è coperto dal gate di parità riportato sopra.
