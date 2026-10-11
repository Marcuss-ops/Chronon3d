# TICKET-VIDEO-CODEC-BOUNDARY — the codec boundary gets one decision language

Status: OPEN — local codec contract and CLI suite are verified; boundary
re-encode and per-layer semantics remain open.

## Contesto

> **Aggiornamento 2026-10-10:** il boundary executor e i contratti codec video sono **VERIFICATI**: `chronon3d_video_contracts_tests` (5/5 casi passati, 25/25 asserzioni), validazione stream e frame order, determinismo serial/parallel e stabilità 900 frame verificati su `chronon3d_cli render`.

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

Questo è il passo 3 del §3 di
[`docs/AUDIT-2026-09-12-VIDEO-CODEC-BOUNDARY.md`](../AUDIT-2026-09-12-VIDEO-CODEC-BOUNDARY.md)
(il passo 4 — per-layer vs per-job — resta aperto).

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

## Re-run mirato — 2026-10-11

Il binario `build/chronon/linux-video-test/tests/chronon3d_cli_tests` è stato
ricostruito dopo le modifiche locali a `video_job_execute_dispatch.cpp` e
`test_packet_assembler_contract.cpp`. La suite completa ha terminato con exit
0: **251/251 casi e 5486/5486 asserzioni passati**. Il filtro
`--test-case='*PacketAssembler*'` è stato inoltre rieseguito: **3/3 casi e 6/6
asserzioni**. Anche `python3 tools/check_architecture.py` è passato: **206/206
regole**. Il checkout era sporco su `e8e06f5b3`; è evidenza CPU sul tree locale,
non certificazione same-SHA pulita o GPU. I follow-up elencati sotto restano
aperti.

## Cosa NON è coperto (follow-up dichiarato)

- **`BoundaryReencode`**: il resolver lo decide per gli intervalli ibridi, ma
  non esiste ancora un boundary re-encoder che consumi un modello di dipendenze
  GOP (finding CB-2 dell'audit).
- **Per-layer vs per-job** (esperimento 4 del §3): la decisione resta un flag di
  job (`command_render_plan.hpp`).
- **Nessuna misura runtime**: il prototipo non cambia il data path, quindi non ha
  effetto misurabile sull'export.

## Demolition Debt

Non applicabile: questo ticket *crea* l'autorità, non rimuove un percorso. Quando
`BoundaryReencode` verrà implementato, l'attuale downgrade
`hybrid_pipeline_unavailable` (`video_execution_resolver.cpp`) diventa il candidato
da demolire, con la sua ragione strutturata già emessa come ricevuta.
