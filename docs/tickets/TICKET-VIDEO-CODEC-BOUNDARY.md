# TICKET-VIDEO-CODEC-BOUNDARY — the codec boundary gets one decision language

## Contesto

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

## Accettazione

| Gate | Eval |
|---|---|
| La tabella pura copre i 4 stati + i due degradi (`interior change`, `unsafe splice`) | unit test, `test_packet_assembler_contract.cpp` |
| Il ponte non produce mai un copy non provato | `availability=false` ⇒ `FullReencode` (unit test) |
| `codec_path` è coerente col piano finale | copy provato ⇒ `CopyPackets`; downgrade/composizione ⇒ `FullReencode` (unit test) |
| Nessun gate di correttezza esistente regredisce | suite `chronon3d_cli_tests` verde; arch gate verde |

## Cosa NON è coperto (follow-up dichiarato)

- **Esecuzione `BoundaryReencode`**: il resolver la decide, ma non esiste ancora un
  boundary re-encoder che consumi un modello di dipendenze di GOP (finding CB-2
  dell'audit).
- **`TrimPackets`**: raggiungibile dalla tabella, non ancora prodotto da alcun
  entry point (serve la mappatura time-range → GOP).
- **Per-layer vs per-job** (esperimento 4 del §3): la decisione resta un flag di
  job (`command_render_plan.hpp`).
- **Nessuna misura runtime**: il prototipo non cambia il data path, quindi non ha
  effetto misurabile sull'export.

## Demolition Debt

Non applicabile: questo ticket *crea* l'autorità, non rimuove un percorso. Quando
`BoundaryReencode` verrà implementato, l'attuale downgrade
`hybrid_pipeline_unavailable` (`video_execution_resolver.cpp`) diventa il candidato
da demolire, con la sua ragione strutturata già emessa come ricevuta.
