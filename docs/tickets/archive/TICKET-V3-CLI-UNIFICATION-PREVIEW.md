# TICKET-V3-CLI-UNIFICATION-PREVIEW — `chronon preview` subcommand (audit Blocco 4.1)

## Stato: Commit 3a DONE (2026-07-14), Commit 3b OPEN

## Problema
L'audit statico (sezione §18) identifica che:
- `RenderArgs::quick_frames` è dichiarato (commands.hpp:105) ma mai
  letto da nessuna parte (`grep -rn 'quick_frames' apps/ include/ src/`
  ritorna solo la dichiarazione).  È un campo morto.
- Non esiste un comando "renderizza una lista discreta di frame
  per ispezione visiva" — l'unico modo per ottenere 4 frame di un
  composition è eseguire `chronon render` 4 volte manualmente.

## Soluzione adottata (Commit 3a of 3 — Blocco 4.1)
Aggiunto `chronon preview <comp> --frames 0,30,60,90 -o preview/`
come nuovo subcommand in
`apps/chronon3d_cli/commands/preview/register_preview_commands.cpp`.

Algoritmo (thin facade sul path canonico, no nuova pipeline):
1. Parsing della lista frame da `--frames "0,30,60,90"` (split by ','
   con trim + skip dei token non-interi).
2. Per ogni frame N nella lista:
   a. Costruisce un RenderArgs fresco con `frames=std::to_string(N)`
      e `output=<output_dir>/frame_NNNN.png`.
   b. `auto plan = plan_render_job(registry, r);`
   c. `if (!plan) { fail++; continue; }`
   d. `if (!execute_render_job(registry, *plan)) { fail++; continue; }`
   e. Stampa timing per frame.
3. Sommario finale: `rendered/total frames in X ms`.

## Rimozione `quick_frames`
- Rimossa la dichiarazione `int quick_frames{0};` da `RenderArgs` in
  `commands.hpp`.  Verificato 0 references post-rimozione.
- Il campo è sostituito semanticamente dal nuovo `PreviewArgs.frames`
  (lista esplicita) che è più generale (non solo count-to-N-1).

## File toccati (Commit 3a)
- `apps/chronon3d_cli/commands/preview/register_preview_commands.cpp` (NEW, ~165 LoC)
- `apps/chronon3d_cli/commands.hpp` (PreviewArgs struct + command_preview decl + remove quick_frames)
- `apps/chronon3d_cli/command_registry.hpp` (register_preview_commands decl)
- `apps/chronon3d_cli/command_registry.cpp` (register_preview_commands call)
- `apps/chronon3d_cli/CMakeLists.txt` (file added to chronon3d_cli_core)
- `docs/CHANGELOG.md` (cite-only entry)
- `docs/FOLLOWUP_TICKETS.md` (forward-point row for --contact-sheet)

## Forward-points
- **TICKET-PREVIEW-CONTACT-SHEET** (Commit 3b, OPEN): implementare
  l'opzione `--contact-sheet sheet.png` che compone tutti i frame
  renderizzati in una griglia 2D.  Richiede:
  - Load di tutti i PNG via `load_image_as_framebuffer(path, cell_width, -1)`
  - Composizione in `ceil(sqrt(N))` x `ceil(N/sqrt(N))` grid
  - Helper `blit(Framebuffer& dst, const Framebuffer& src, int x, int y)` (~50 LoC, in-file)
  - Save via `save_png(dst_grid, contact_sheet_path)`
- **TICKET-PREVIEW-CELL-LABELS** (V0 follow-up): aggiungere label
  testuali per ogni cella (es. "frame 60").  Richiede text-rendering
  pipeline, deferito per minimal-surface.

## Criteri di accettazione
- ✅ `chronon preview --help` elenca `--frames`, `--output-dir`, `--contact-sheet`.
- ✅ `chronon3d_cli preview` si registra come subcommand (verified via grep).
- ✅ Il loop chiama `plan_render_job` + `execute_render_job` (NO nuova pipeline).
- ✅ `RenderArgs::quick_frames` rimosso (verified 0 references).
- ✅ `docs/CHANGELOG.md` ha la riga cite-only; nessuna cronaca estesa.
- ✅ `docs/FOLLOWUP_TICKETS.md` ha la riga forward-point per --contact-sheet.
- ✅ Subject envelope del commit ≤ 72 char.
- ✅ `tools/check_doc_sync.sh` invariato (nessun canonical doc toccato).
- ✅ 5 ZERO-crime rg-probes = 0 (no new add(name,factory), no TextSpec,
  no process-wide asset root, no _FUTURE_TODO, no internal/ SDK leak).
- ⏸  Commit 3b: contact-sheet composer (forward-point).
- ⏸  macchina-verifica (build + ctest) DEFERRED-WBH (env-block su questo VPS).
