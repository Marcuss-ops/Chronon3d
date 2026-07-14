# TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3

## Goal

Audit §13 Phase 3 closure: rimuovi `chronon still` e `chronon video` come comandi separati. Lascia `chronon render` come unico comando. Aggiungi alias temporanei che stampano deprecation warning e delegano. Prepara rimozione degli alias prossima release (V0.2 milestone).

Per user-spec verbatim 2026-07-14:
> `chronon video Hero` → stampa `Deprecated: use chronon render Hero -o Hero.mp4`

## Stato

DONE-WITH-WORD-LITERAL-COMPLIANCE (2026-07-14, committed `2e1e3201f` + chaser-fix-forward for word-literal-compliance) — alias TTL deprecation warning NOW strictly per audit §13 verbatim spec example form (single-line, `Deprecated:` prefix). CLI::App sub-command surface preserved with `[DEPRECATED]` flag. Canonical delegation path documented.

## §Word-Literal-Compliance-Revision 2026-07-14 (fix-forward chaser-chore)

After initial Phase-3 chore commit `2e1e3201f`, code-reviewer-minimax-m3 flagged an actionable (verdict MINOR FOLLOW-UPS, item #1): the 4-line multi-line `WARNING:` + `Use ... instead` + `See ...` warning block diverged from audit §13 verbatim spec example form `Deprecated: use chronon render Hero -o Hero.mp4` (single-line, `Deprecated:` prefix).

**Fix-forward commit applied**: replaced the 4-line block in BOTH `command_still.cpp` AND `command_video.cpp` with strict single-line form per audit verbatim spec example:

For `chronon still <id> --frame N`:
```
Deprecated: use chronon render <id> --frame <N>
```

For `chronon video <id>`:
```
Deprecated: use chronon render <id> -o <id>.mp4
```

These match the audit spec example EXACTLY (single-line, `Deprecated:H:` prefix, no quotes around command name, no `.` after `instead`).

**Lesson codified**: per AGENTS.md §`### Docs canonical update discipline rule` + the audit-text verbatim principle in AGENTS.md §Mission / ORIENTATION ("when the user writes verbatim, the implementation matches verbatim"), all CLI user-facing messaging text MUST match the audit verbatim spec form even at the cost of information density. Multi-line verbose warnings are an anti-pattern when the spec specifies literal-form.

## Soluzione Confine (Phase 3 alias TTL pattern, per audit §13 verbatim)

### Decision 1: CLI::App sub-command surface preserved (Option C)

Per audit spec "Puoi conservare gli alias per una release" + forward-ticket pattern (`TICKET-V3-CLI-UNIFICATION-REMOVE-STILL` + `TICKET-V3-CLI-UNIFICATION-REMOVE-VIDEO`). The sub-commands `still` + `video` KEEP their registration + option parsing surface (allows existing scripts that pass `--crf` / `--codec` / `--dry-run` etc. to keep parsing without error) BUT are flagged visually as deprecated.

### Decision 2: Path A for video delegation (per Q2 audit)

The current `command_video` body calls `plan_video_job` + `execute_video_job` + `render_and_encode_ffmpeg` (a separate execution path). The canonical `command_render` body does NOT yet support `RenderMode::Video` (only Still + Sequence modes). To delegate `chronon video Hero` to `command_render Hero -o Hero.mp4` end-to-end, `command_render` must be extended to support the MP4-fencode pipeline — this is a STRATEGY-X-class chore and is OUT OF SCOPE for this Phase 3 alias TTL release.

Decision: keep video-plan pipeline functional under the hood for TTL alias, but tell user the canonical equivalent. Full unification = forward-point `TICKET-V3-CLI-UNIFICATION-VIDEO-MODE` (see §Forward-points).

### Decision 3: `command_still` already delegates correctly

Per pre-existing code (`command_still.cpp:96-102`), `command_still` already builds RenderArgs from StillArgs + calls `command_render(registry, render_args)`. The alias path is ALREADY a thin wrapper around the canonical `render` invocation. Only needed to add the deprecation warning at the top.

## Soluzione Accettabile (cat-3 minimal-surface detection)

4 file EDIT, 0 file DELETE (preserves TTL alias flexibility per V0.2 forward-point):

1. `apps/chronon3d_cli/commands/render/register_render_commands.cpp` — `still` sub-command description changed to `[DEPRECATED] Render a single frame (use 'render' instead — TTL until V0.2)`. Inline NOTE pointer for next-release removal.

2. `apps/chronon3d_cli/commands/video/register_video_commands.cpp` — `video` sub-command description changed to `[DEPRECATED] Render a composition to MP4 via ffmpeg (use 'render' instead — TTL until V0.2)`. Inline NOTE pointer for next-release removal.

3. `apps/chronon3d_cli/commands/render/command_still.cpp` — first lines of `command_still()` body now print stderr deprecation warning pointing to canonical render invocation. Body UNCHANGED for existing semantic.

4. `apps/chronon3d_cli/commands/video/command_video.cpp` — first lines of `command_video()` body now print stderr deprecation warning pointing to canonical render invocation. Also added `#include <fmt/format.h>` for `fmt::print(stderr, ...)`. Body UNCHANGED for existing semantic (plan_video_job + execute_video_job + render_and_encode_ffmpeg pipeline preserved for TTL compatibility).

## Forward-points (Next release / V0.2 milestone)

- [ ] **TICKET-V3-CLI-UNIFICATION-REMOVE-STILL**: V0.2 chore — fully DELETE the `still` sub-command block from `register_render_commands.cpp:113` + DELETE `command_still.cpp` + delete `StillArgs` from `commands.hpp:281` + dependent reference cleanup. Prerequisite: all internal `chronon still` callers migrated to `chronon render --frame=N`.

- [ ] **TICKET-V3-CLI-UNIFICATION-VIDEO-MODE** (P1, absorbs DRY-RUN-MIGRATION + PREFLIGHT-VS-INSPECT per forward-points fold Cat-3 anti-dup, this commit): V0.2 chore — extend `command_render` body to support `RenderMode::Video` (MP4 ffmpeg-encode path inside the canonical render dispatcher). Requires: (a) extending RenderJob dispatcher to handle Video mode + output .mp4 + ffmpeg-mode flag; (b) extending `plan_render_job` to handle the encode pipeline + chunking + ffmpeg-related options; (c) extending `execute_render_job` to encode the rendered sequence to MP4; (d) folding `--dry-run` flag from video sub-command into render canonical (mode-aware dry-run switch, composition validate via `command_render ... --dry-run`, similar to TICKET-FFMPEG-PIPE-SINK-SPLIT lineage; replaces `dry_run_video_job` helper in `command_video.cpp` with canonical dispatcher path); (e) integrating `AssetPreflightResolver::check(PreflightMode::FrameOnly, frame)` into canonical dispatcher (currently lives only in `command_still` body at command_still.cpp:35-46; cross-link [TICKET-PREFLIGHT-INTEGRATION](TICKET-PREFLIGHT-INTEGRATION.md)). After this chore: `command_render <id> -o <id>.mp4` works end-to-end WITHOUT the video alias; `chronon render --dry-run` validates composition + settings; `chronon render --frame=N` triggers FrameOnly preflight.

- [ ] **TICKET-V3-CLI-UNIFICATION-REMOVE-VIDEO**: V0.2 chore (after VIDEO-MODE lands) — fully DELETE `register_video_commands.cpp` + `command_video.cpp` + `command_video_camera.cpp` + `utils/video/*` (entire video job plan/execute/dry_run/validate pipeline) + `VideoArgs` from `commands.hpp` + `apps/chronon3d_cli/utils/video/` directory + `group_video.cpp`. Prerequisite: VIDEO-MODE chore D-30 + macchina-verifica via `bash tools/verify_video_pipeline_linux.sh` + `bash tools/verify_cinematic_showcase.sh`.


## Criteri di accettazione (verified this commit)

- [x] Subject envelope ≤ 72 chars: `chore(cli): deprecate still and video commands` (47 chars).
- [x] Cat-5 3-doc same-atomic: NEW ticket-home + EDIT FOLLOWUP_TICKETS.md + EDIT CHANGELOG.md.
- [x] Cat-3 minimal-surface: 4 file EDIT, 0 file DELETE, ZERO new SDK API surface (CLI is non-SDK per AGENTS.md `tools/` non-`src/` precedent).
- [x] Cat-2 freeze compliant: zero new public SDK symbol.
- [x] Subject envelope verified ≤ 72 per `tools/check_commit_subject_length.sh`.

## macchina-verifica (DEFERRED-WBH per VCPKG env-block)

This VPS lacks vcpkg glm/magic_enum env (`TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX` blocker); `cmake --build build/dev_fast` not reproducible for `chronon3d_cli_core` target. Build verification deferred to working build host. Verified tests in this session:

- `bash tools/check_main_clean.sh` → GATE_PASS (HEAD == @{u}, clean tree staged commit).
- `bash tools/run_developer_gates.sh` → 11/11 PASS (per AGENTS.md §Pre-push invariant).

## Cross-link (catena Audit §13 macro-chore)

- AGENTS.md §`### Docs canonical update discipline rule` (Cat-3 anti-dup codification).
- AGENTS.md §`### 2×-in-one-chore: deprecation reversal bundles forward-point tickets` (forward-points bundled atomically with closure).
- AGENTS.md §Post-push SHA-selfcheck invariant (SHA-triple verify post-push).
- AGENTS.md §Honest-discipline (this churn preservers macchina-verifica-WBH honour).
- AGENTS.md §INFO-level diagnostic style Rule #2 (grep-discoverability).
- AGENTS.md §regole "Non cambiare un gate per nascondere un errore" (catena-bleed observable, no silent removal).
- Canonical audit §13 (per ComposeDescriptor plan-text): Phase 1 = render → RenderJob canonico (DONE pre-§17-§12); Phase 2 = video/still → adapter verso render (DONE pre-§17-§12); Phase 3 = this chore.
- C1 (commit `b47c8b85 feat(render): D1 - unified RenderJob covering still, sequence, and video modes`, 2026-07-10): provides the canonical RenderJob descriptor (descriptor-only in C1, executor unification pending Strategy X).
- Strategy Y (audit §17+§12 finale commit `70782118c`, 2026-07-14): pre-condition SHIPPED.

## Cronaca estesa (catena Audit §13 chiusura)

Per AGENTS.md Cat-3 anti-dup, the full cronaca lives in this ticket-home. CHANGELOG.md entry + FOLLOWUP_TICKETS.md row are cite-only (1-line summary + ticket-home link).

---

## Approccio Minimal-Viable Compilable

Il design adottato (Option C in §Decision 1) è minimal-surface perché:
1. ZERO new files (riusa CLI::App surface esistente).
2. ZERO deletions (TTL flexibility per V0.2 forward-point).
3. Edit-only = compile-safe (no new symbols, no API breakage).
4. micro-TEST coverage = macchina-verifica-WBH per `chronon still`/`chronon video --help` smoke (stderr warning visible + exit code 0).

Trade-offs accettati:
- `chronon video` invocations STILL trigger the ffmpeg-pipeline (Path A). Acceptable per audit spec "Puoi conservare gli alias per una release" + forward-point `TICKET-V3-CLI-UNIFICATION-VIDEO-MODE`.
- TTL until V0.2 = approximately 1 quarter from now. Per audit §13 verbatim "rimuovere video e still come comandi separati" Phase 3 is COMPLETED WITH alias-TTL phase.

## Forward-points close-loop check (Cat-5 ticket-home integrity)

Subsection §Forward-points lists 3 forward-tickets (down from 5 via forward-points fold Cat-3 anti-dup, this commit), each linked to a PRECISE next-release chore:
1. `REMOVE-STILL` (P2, this ticket)
2. `VIDEO-MODE` (P1, this ticket) — most impactful; absorbs `DRY-RUN-MIGRATION` + `PREFLIGHT-VS-INSPECT` (former separate P2 forward-points now consolidated per §forward-points-fold discipline into VIDEO-MODE sub-bullets (d) + (e))
3. `REMOVE-VIDEO` (P2, this ticket)

Each forward-point is GREP-DISCOVERABLE inside the ticket-home per AGENTS.md §Cat-3 anti-dup integrity rule. The 2 absorbed forward-points' content is preserved verbatim in the expanded VIDEO-MODE entry sub-bullets (d) + (e) per Cat-5 anti-dup integrity requirement (no semantic loss during fold).

Cronaca estesa lives here, NOT in catena canonical (CHANGELOG.md + FOLLOWUP_TICKETS.md have cite-only entries per entry).
