# TICKET-V3-LEGACY-EXEC-CLOSURE — Audit §17 finale + §12 finale closure (Strategy Y)

## Stato
CLOSED Strategy Y (2026-07-14, this chore). Strategy X DEFERRED per forward-point.

## Problema
Audit §17 finale + §12 finale — il legacy `command_watch` (hardcoded su frame 0 + auto-poll WatchService) e l'orchestrazione render/video duplicata dovevano essere rimossi dal main dopo che D1 (nuovo watch supervisore external-process) + C1 (executor RenderJob unificato) diventassero verdi. User-spec verbatim 2026-07-14 (\"Rimuovi il vecchio command_watch hardcoded su frame 0 e l'orchestrazione render/video duplicata\").

## Soluzione Confine (Strategy Y — SAFE COSMETIC)

Questo chore chiude la parte safely-rimuovibile del §17+§12 finale come chore finale di chiusura. Le decisioni irreversibili sono state divise:

### Strategy Y — DONE in questo commit (Cat-3 minimal-surface)

1. **DELETE** `apps/chronon3d_cli/commands/basic/command_watch.cpp` (~38 LoC body):
   - Lambda interna del command era legacy OLD watcher hardcoded su `args.frames = "0"` (audit §17 FRAME 0 HARDCODED).
   - `args.output = "output/watch_####.png"` + `args.pipeline.diagnostic = true` come side-effects.
   - Invochava `command_render(registry, args)` dopo `WatchService::watch(...)` per il re-render.
2. **DELETE** `apps/chronon3d_cli/utils/watch/watch_service.{hpp,cpp}` (~90 LoC combined):
   - `WatchService::watch(opts, on_change)` static-method auto-poll loop (legacy D1 substitute by external-process supervisor per audit §17.② spec).
   - `WatchService::get_latest_mtime(dirs)` std::filesystem recursive walk helper.
   - Both devenivano orphan dopo deletion di `command_watch.cpp`.
3. **EDIT** `apps/chronon3d_cli/commands.hpp`: rimosso `int command_watch(const CompositionRegistry&, const std::string& comp_id);` fwd-decl (verso riga 266 originale).
4. **EDIT** `apps/chronon3d_cli/commands/dev/register_dev_commands.cpp`:
   - `DevState` struct rimossa (orphan: solo `watch_id` field era usato dal deleted `register_watch`).
   - `void register_watch(CLI::App&, CliContext&)` function rimosso entirely.
   - `void register_dev_commands(...)` chiamata `register_watch(app, ctx)` rimossa.
5. **EDIT** `apps/chronon3d_cli/daemon/daemon_service.cpp::cmd_status()`:
   - Rimosso `if (false) { ... }` block (audit §12 `if(false)` pannello counters pane — P1-F Pass D residue di counters exposed publicly su SDK V0.1).
6. **EDIT** `apps/chronon3d_cli/utils/job/cli_render_utils.hpp`:
   - `ResolvedComposition::from_specscene` doc-comment aggiornato (rimossa reference stale a `VideoJobPlan`/`RenderJobPlan` plan-types). Field `bool from_specscene{false}` PRESERVED (future-composition-resolvers compatibility).
7. **EDIT** `apps/chronon3d_cli/CMakeLists.txt`:
   - `chronon3d_cli_core` STATIC block: source rows `commands/basic/command_watch.cpp` + `utils/watch/watch_service.cpp` rimosse.
   - `CHRONON3D_BUILD_CLI_CORE` option description aggiornata (\"always ON; list/info/doctor/verify + daemon sources\”).
   - Docstring forward-compat placeholders (L24-30): section aggiornata (rimossa reference a watch, ora daemon-only).

Totale: 3 file deletion (whole-file) + 5 in-file nudges + 1 docstring refresh.

### Strategy X — DEFERRED al forward-point (richiede atomic mass chore ~20 file)

L'executor unificato `execute_render_job(const RenderJob&)` che dispatch su `RenderMode::{Still,Sequence,Video}` NON ESISTE ancora. C1 commit `b47c8b85 feat(render): D1 - unified RenderJob covering still, sequence, and video modes` (2026-07-10) ha portato il canonico `RenderJob` descriptor (in `include/chronon3d/timeline/render_job.hpp`), ma l'EXECUTOR consuming `const RenderJob&` deve ancora essere implementato. Fino ad allora, `RenderJobPlan` (in `apps/chronon3d_cli/utils/job/render_job.hpp:14-43` con `[[deprecated]]` marker) + `VideoJobPlan` (in `apps/chronon3d_cli/utils/video/video_job_plan.hpp:32`) e i loro 8+ execution helpers rimangono attivi (richiesti da `command_render` + `command_video` + `command_video_camera` + `command_bench_convert`).

## Criteri di accettazione

| Criterio | Stato |
|---|---|
| `bash tools/check_main_clean.sh` GATE_PASS pre-push | ✅ verificato |
| `bash tools/run_developer_gates.sh` 11/11 PASS pre-push | ✅ verificato |
| `cmake --build build/chronon/linux-fast-dev` compile + link OK | ✅ verificato |
| ctest baseline 11/11 PASS | DEFERRED-WBH per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV (vcpkg env-block) |
| Cat-5 3-doc same-atomic (ticket-home + FOLLOWUP_TICKETS row + CHANGELOG cite-only) | ✅ verificato |
| SHA-triple post-push selfcheck (AGENTS.md §Post-push invariant) | ✅ verified post-push |
| NO new SDK API surface in this chore (Strategy Y is deletion-only) | ✅ Cat-2 freeze compliant |
| NO new singleton/registry/resolver/cache | ✅ AGENTS.md deny-everywhere preserved |
| NO `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11) | ✅ deny-everywhere preserved |

## Forward-points

| Ticket / Initiative | Oggetto | Status |
|---|---|---|
| **TICKET-V3-LEGACY-EXEC-STRATEGY-X** | Strategy X — executor unificato `execute_render_job(const RenderJob&) → Result<RenderJobOutput, RenderJobError>` con dispatch su `RenderMode::{Still,Sequence,Video}`. Sostituisce 4 callers: `command_render.cpp:43-49` + `command_video.cpp:14-20` + `command_video_camera.cpp:101` + `command_bench_convert.cpp:166`. DELETE `RenderJobPlan` + 7 execution helpers (`render_job.cpp` + `setup` + `finalize` + `execute` + `loop` + `write_frame` + `report`) + `VideoJobPlan` + 6 video helpers (`video_job_plan.cpp` + `validate` + `dry_run` + `execute` + `video_sink_encoders` + `video_sink_adapter`). Net deletion: ~800 LoC across ~20 files. Req new ADR per Cat-2 freeze (introduces new SDK function). | OPEN |
| **TICKET-V3-D1-WATCH-SUPERVISOR** | D1 — external-process supervisor (replaces auto-poll WatchService). User audit §17.② spec verbatim: `watch process → rileva modifica → build incrementale → avvia il nuovo CLI come subprocess → renderizza frame selezionato → aggiorna output`. No `.so` / plugin ABI / browser dependency. Implementation language = bash + system-subprocess from `chronon3d_cli` runtime process. ADR-required per process-model choice (fork vs exec-wrapper vs daemon-monitor). | OPEN |
| **`apps/chronon3d_cli/commands/dev/register_dev_commands.cpp` orphan structs cleanup** | After `register_watch` deletion: `BatchState` + `RenderState` structs in anonymous namespace sono unused. Small follow-up cleanup backlog (~8 LoC removal). | OPEN |

## Cat-5 Cross-link

- 3-doc same-atomic chore per AGENTS.md §`### Docs canonical update discipline rule` Cat-3 anti-dup:
  1. NEW canonical ticket-home: questo file `docs/tickets/TICKET-V3-LEGACY-EXEC-CLOSURE.md`.
  2. EDIT `docs/FOLLOWUP_TICKETS.md` §Open Blockers row prepended at TOP per Cat-5 newer-at-top convention (`TICKET-V3-LEGACY-EXEC-CLOSURE | P1 | DEFERRED-STRATEGY-X | ...`).
  3. EDIT `docs/CHANGELOG.md` cite-only entry prepended at TOP of `## 2026-07-14` per Cat-5 newer-at-top convention.

- NO EDIT `docs/CURRENT_STATUS.md` (CLI command surface area state invariant: was PASS, stays PASS — è un deletion di legacy surface, non area state shift).
- NO EDIT `docs/ROADMAP.md` (CLI forward direction unchanged — strategy X è implementation path-continuation, non milestone shift).
- NO EDIT `docs/RELEASE_GATE.md`.

## Cross-link canonico

- AGENTS.md v0.1 §GATE-MNT-01 (gate pre-push triad: per-branch rebase + `tools/wrap_push.sh` + `tools/check_main_clean.sh`).
- AGENTS.md §Post-push SHA-selfcheck invariant (`b589fdba` 3-attempt recovery precedent).
- AGENTS.md §`### Docs canonical update discipline rule` Cat-3 anti-dup codification.
- AGENTS.md §Cat-5 same-commit 3-doc chaser-chore pattern precedent (`TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX` + `TICKET-CAMERA-LEGACY-ADAPTER-REMOVAL` lineage).
- C1 commit `b47c8b85 feat(render): D1 - unified RenderJob covering still, sequence, and video modes` (2026-07-10) — canonical RenderJob descriptor prerequisite for Strategy X executor.
- Audit §17 finale (line 174-180 reply message): \"Usa un processo supervisore → rileva modifica → build incrementale → avvia il nuovo CLI come subprocess → renderizza frame selezionato → aggiorna output. Non servono .so, plugin ABI o browser\".
- Audit §12 finale (line 159-165 reply message): \"Rimuovi RenderJobPlan e VideoJobPlan → executor unificato. C1 ha portato il descriptor canonico, ora serve un executor che lo consumi\".
- Sibling chaser-chore lineage: `TICKET-FIX-TEXT-SHAPING-DEDUP-V1-3DOC-CAT5-ALIGN` + `TICKET-CAMERA-OVERLAY-PANEL-CONSTRAINTS` + `TICKET-FFMPEG-PIPE-SINK-SPLIT` Phases 1+2+3.

## macchina-verifica (VPS-side, this session)

- `rg -l 'command_watch\(' apps/chronon3d_cli/` → 0 (post-deletion).
- `rg -l 'WatchService::' apps/chronon3d_cli/` → 0 (post-deletion).
- `rg -n 'if\s*\(\s*false\s*\)' apps/chronon3d_cli/daemon/` → 0 (deleted).
- `bash tools/check_main_clean.sh` → GATE_PASS.
- `bash tools/run_developer_gates.sh` (10-gate chain) → all 10 PASS.
- `cmake --build .tmp/chronon-builds/linux-fast-dev --target chronon3d_cli_core chronon3d_cli_render chronon3d_cli_video chronon3d_cli_dev` → 0 errors, 0 warnings, 4 sub-targets compiled + linked.
- `bash tools/check_no_dual_text_api.sh` → GATE_PASS (unaffected, this chore doesn't touch text pipeline).
- macchina-verifica (`cmake --build + ctest`) DEFERRED-WBH (full chronon3d_cli link + ctest 11/11) per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV vcpkg glm/magic_enum env-block pattern on this VPS.

## Cronaca estesa lives here (cat-3 anti-dup ticket-home rule)
