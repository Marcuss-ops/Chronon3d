# TICKET-V3-CLI-UNIFICATION-VIDEO-MODE

## Goal

Extend `command_render` body to support `RenderMode::Video` via unified `execute_render_job` switch dispatch (Still | Sequence | Video). After this chore: `chronon render Hero -o Hero.mp4` works end-to-end WITHOUT the `chronon video` alias; `chronon render --dry-run` validates composition + settings; `chronon render --frame=N` triggers FrameOnly preflight via canonical dispatcher. Required pre-condition for `TICKET-V3-CLI-UNIFICATION-REMOVE-VIDEO` (which cannot fire until VIDEO-MODE is GREEN).

This ticket's 5 sub-bullets (a)–(e) are inherited from `docs/tickets/TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3.md` §Forward-points, expanded post forward-points fold Cat-3 anti-dup integrity closure (commit `37cd407d7`).

## Stato

DESIGN-SESSION-COMPLETE (2026-07-14) — Phase 0 chaser-cycle ticket-open via Cat-5 3-doc atomic (NEW this ticket-home + EDIT FOLLOWUP_TICKETS §Open Blockers + EDIT CHANGELOG chaser cite-only). Source code is OFF-LIMITS this phase per AGENTS.md "Fare PR piccole e mirate" + Cat-3 ticket-open convention (zero source modification, design-only deliverable). Implementation phases (Phase 1–4) bound to subsequent forward-chore commits.

## §Design-Session-Outcome (thinker-with-files-gemini synthesized 2026-07-14)

### Section A — Question-by-question recommended choices

**(A.1) RenderArgs Extension.** Recommend **sub-struct**: `VideoSettings video;` nested inside `cli::RenderArgs`. Rationale: Cat-3 zero-new-SDK-symbol compliance; CLI parser hygiene (5+ video flags stay grouped); ABI-stability under Cat-2 freeze (mirrors existing `chronon3d::chronon3d::VideoSettings` aliasing pattern); zero call-site churn during Phase 1 (sub-struct absent for Still, present for Video).

**(A.2) execute_render_job switch dispatch.** Canonical form:

```cpp
// inside execute_render_job (or its CLI-package wrapper)
switch (plan.job.mode) {
case RenderMode::Still:
    // single frame: SoftwareRenderer → PNG writer → plan.job.output
    break;
case RenderMode::Sequence:
    // ranged loop: for f in [first_frame, last_frame] → SoftwareRenderer → string-format writer
    break;
case RenderMode::Video:
    // ranged loop: for f in [first_frame, last_frame] → render → pipe to ffmpeg
    // uses plan.job.video_settings (fps/crf/codec/encode_preset) + plan.chunks
    break;
}
```

Common setup (renderer warmup + telemetry reset + AssetPreflightResolver cache priming) BEFORE the switch is unified; renderer shutdown AFTER the switch is unified. Per-mode logic stays minimal.

**(A.3) VideoArgs struct + command_video body after fold.** Recommend **Option (c)**: keep `VideoArgs` standalone (Cat-2 ABI freeze — alias TTL until V0.2), but `command_video` body converts `VideoArgs → RenderArgs` (mapping `start/end/fps/crf/codec/encode_preset/dry_run` into `RenderArgs::frames + RenderArgs::video`) then delegates to canonical `command_render`. Print deprecation warning + delegate to canonical. `VideoArgs` struct DROPS in V0.2 alongside `command_video`/`register_video_commands.cpp`/`utils/video/*.cpp` — tracked by `TICKET-V3-CLI-UNIFICATION-REMOVE-VIDEO`.

**(A.4) AssetPreflightResolver FrameOnly integration.** Placement: inside `plan_render_job`. Per-mode:
- `RenderMode::Still` → `AssetPreflightResolver::check(scene, resolver, PreflightMode::FrameOnly, job.still_frame)`.
- `RenderMode::Sequence` → `AssetPreflightResolver::check(scene, resolver, PreflightMode::FullComposition)`.
- `RenderMode::Video` → `AssetPreflightResolver::check(scene, resolver, PreflightMode::FullComposition)` (full composition check because every frame depends on the same asset set).

Single point of truth in `plan_render_job`; removes the door-site call in `command_still.cpp:48-49`.

**(A.5) --dry-run flag fold.** Placement: early-out in `command_render` AFTER `plan_render_job` returns. `plan_render_job` performs preflight + validation but does NOT write any output. Dry-run semantics: print the resolved plan (mode + frame range + encoder options if Video) to stderr + exit 0. Zero ffmpeg side-effects; zero file system writes.

`dry_run` is a CLI execution flag → stays in `RenderArgs`, NOT in `RenderJob` (SDK descriptor is execution-agnostic).

**(A.6) RenderJob discriminant context.** `RenderJob` carries:
- `VideoSettings video;` — sub-struct with `fps:int`, `crf:int`, `codec:std::string`, `encode_preset:std::string`.
- `mode` discriminant: still/sequence/video.
- `first_frame:Frame`, `last_frame:Frame` + `still_frame:Frame`.
- `output:std::string`.

(`dry_run` does NOT live in `RenderJob` — CLI-only artifact.)

**(A.7) Minimum-viable test surface expansion.** Beyond existing `tests/video/test_ffmpeg_pipe_*.cpp` (Phase-3 TICKET-FFMPEG-PIPE-SINK-SPLIT):
- `tests/video/test_render_video_mode_dispatch.cpp` — `chronon render comp --frames 0-10 -o out.mp4` infers `RenderMode::Video` from extension + dispatches through canonical, not legacy alias.
- `tests/video/test_render_dry_run_flag.cpp` — `--dry-run` exits 0 with empty output directory + plan printed to stderr.
- Existing `tests/assets/test_asset_preflight_resolver.cpp` regression lock for FrameOnly preflight trigger.

### Section B — Phased migration plan (each phase ≤1 atomic commit)

**Phase 0 (THIS TICKET-OPEN CHASER-CYCLE):** Design session only. 0 source touched. Cat-5 3-doc atomic doc-only.

**Phase 1: RenderArgs + RenderJob sub-struct + register_render_commands video flags.**
- EDIT `apps/chronon3d_cli/commands.hpp` line 91: append `VideoSettings video;` field to `struct RenderArgs`.
- EDIT `include/chronon3d/timeline/render_job.hpp` line 71: append `VideoSettings video;` field to `struct RenderJob` (mirrors `RenderArgs` sub-struct shape).
- EDIT `apps/chronon3d_cli/commands/render/register_render_commands.cpp`: parse `--fps`, `--crf`, `--codec`, `--encode-preset`, `--dry-run` directly into `args.video` sub-struct (canonical side).
- macchina-verifica: `chronon render --help` exposes new video flags.

**Phase 2: Preflight fold + dry-run early-out.**
- EDIT `apps/chronon3d_cli/commands/render/command_render.cpp` body: integrate `AssetPreflightResolver::check` dispatch per (A.4); wire `--dry-run` early-out per (A.5).
- EDIT `apps/chronon3d_cli/commands/render/command_still.cpp` lines 48-49: REMOVE the door-site FrameOnly preflight call (now single-source-of-truth in canonical).
- EDIT `apps/chronon3d_cli/utils/job/render_job.cpp` + `render_job_setup.hpp`: ensure preflight cache priming is unified across modes.
- macchina-verifica: existing `tests/assets/test_asset_preflight_resolver.cpp` green; `chronon render comp --frame=0 -o still.png` works end-to-end without FrameOnly-preflight duplication.

**Phase 3: execute_render_job RenderMode switch dispatch (the meat).**
- EDIT `apps/chronon3d_cli/utils/job/render_job.cpp`: extend `execute_render_job` body with the 3-way switch per (A.2); connect Video branch to existing `render_and_encode_ffmpeg` (preserve telemetry aggregates intact).
- EDIT `apps/chronon3d_cli/utils/video/video_job_execute.cpp`: extract `render_and_encode_ffmpeg` body so it can be called directly from canonical (it currently has internal sequencing — Phase 3 must preserve chunking + telemetry + cancellation token handling).
- macchina-verifica: `chronon render comp --frames 0-10 -o out.mp4` produces valid MP4; existing `tests/video/test_ffmpeg_pipe_sink_*.cpp` regression lock held.

**Phase 4: command_video alias TTL delegation (post Phase 3 GREEN).**
- EDIT `apps/chronon3d_cli/commands/video/command_video.cpp`: gut body to `VideoArgs → RenderArgs` conversion + deprecation warning + delegate to canonical `command_render(registry, converted_args)`.
- DELETE `apps/chronon3d_cli/utils/video/video_job_dry_run.cpp` + `video_job_plan.cpp` (replaced by canonical preflight + plan).
- macchina-verifica: `chronon video comp -o out.mp4` produces byte-equivalent output to `chronon render comp -o out.mp4` (alias TTL pre-condition met).

**Phase 5: REMOVE-VIDEO pre-condition GREEN.**
- macchina-verifica: `bash tools/run_developer_gates.sh` 11/11 green; `bash tools/verify_video_pipeline_linux.sh` PASS.
- Forward-ticket `TICKET-V3-CLI-UNIFICATION-REMOVE-VIDEO` becomes executable.

### Section C — Risk Register

1. **FFmpeg pipeline telemetry divergence.** Probability: medium × Impact: high. Trigger: bypassing `video_job_execute` telemetry aggregates during the fold. Mitigation: retain the core `render_and_encode_ffmpeg` dispatch block exactly as-is initially; wrap inside `execute_render_job` switch. Gate: end-to-end sqlite telemetry test.

2. **Preflight per-call performance regression.** Probability: low × Impact: medium. Trigger: applying `FullComposition` preflight implicitly inside `plan_render_job` causing double-evaluation. Mitigation: ensure preflight cache priming happens BEFORE the switch + the resolver is reused across modes. Gate: `tests/core/timeline/test_sequence_integration.cpp` budget assertion.

3. **Chunking parameter mapping mismatch.** Probability: low × Impact: high. Trigger: losing parallel chunk parameters during `RenderArgs::video → RenderJob::video → FfmpegExportOptions` conversion. Mitigation: explicit test verifying `--chunks` flows correctly down to `PipeExportResult`. Gate: `tests/video/test_render_video_mode_dispatch.cpp` chunks-3 case.

4. **ABI churn risk via `RenderArgs` mutation.** Probability: low × Impact: medium. Trigger: adding fields to `RenderArgs` breaks 200+ pre-B2 callers in `tests/` + `content/showcases/` + `apps/` (forward-point from `TICKET-COMPOSITIONDESCRIPTOR-MIGRATION`). Mitigation: append-only field; existing positional init still works. Gate: canonical `tests/cli_tests.cmake` green.

5. **Cat-2 freeze violation risk.** Probability: low × Impact: high. Trigger: extending `include/chronon3d/timeline/render_job.hpp` accidentally adds a non-backwards-compatible field default. Mitigation: append-only field with no default; existing positional struct init must still compile via designated-initializer C++20 syntax. Gate: `TICKET-COMPOSITIONDESCRIPTOR-MIGRATION`'s 200+ callers compile clean.

### Section D — Open questions for followup tickets

- **TICKET-FFMPEG-SINK-ABSTRACTION**: `render_and_encode_ffmpeg` is currently tightly coupled to chunk/pipe details. Should it be refactored into an isolated `RenderJobSink` interface in `include/chronon3d/timeline/`?
- **TICKET-REMOVE-VIDEO-ARGS** (already registered via `TICKET-V3-CLI-UNIFICATION-REMOVE-VIDEO`): delete `struct VideoArgs` + `register_video_commands.cpp` + `utils/video/*.cpp` in V0.2.
- **TICKET-DRYRUN-CANONICALIZATION**: standardize dry-run console output format to JSON for tooling consumption (currently free-form stderr).

## §Irreducible Trade-off

Retaining `VideoArgs` standalone (Cat-2 ABI freeze compliance) guarantees `chronon video` alias TTL stability through V0.2 — at the cost of maintaining TWO arg-structures (`RenderArgs::video` sub-struct + `VideoArgs`) which must be kept in sync between `register_render_commands.cpp` and `register_video_commands.cpp` until REMOVE-VIDEO fires. This is the canonical debt we're accepting to keep the SDK surface frozen.

## Soluzione Confine (Phase-1 compiler-safe strategy)

Phase 1 mirror pattern (per `TICKET-COMPOSITIONDESCRIPTOR-MIGRATION` lineage precedent): append-only `VideoSettings` field in BOTH `RenderArgs` (CLI) + `RenderJob` (SDK) with no default value. Existing positional struct init continues to compile per C++20 designated-initializer syntax; existing CLI callers unaffected.

## Soluzione Accettabile (cat-3 minimal-surface detection per phase)

Each Phase 1-4:
- ZERO new SDK symbol in `include/chronon3d/` (Cat-2 freeze).
- ZERO new singleton/registry/resolver/cache (AGENTS.md deny-everywhere).
- ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 deny-everywhere).
- ZERO new INTERFACE library.
- Phase 4 specific: 1 DELETE (video_job_dry_run.cpp) + 1 DELETE (video_job_plan.cpp) ≈ 250 LoC total reclaimed.

## Forward-points (after Phase 5 ships)

- [x] **THIS TICKET Phase 0** (parent ticket, in §Open Blockers P1) — design session complete.
- (Phase-1–4 forward-chores to be opened post-Phase-0 chaser-cycle closure).
- [ ] **TICKET-V3-CLI-UNIFICATION-REMOVE-VIDEO** (already registered, P2): delete `register_video_commands.cpp` + `command_video.cpp` + `command_video_camera.cpp` + `utils/video/*` + `VideoArgs` from `commands.hpp` + `group_video.cpp` directory. Prerequisite: Phase 5 GREEN.
- [ ] **TICKET-FFMPEG-SINK-ABSTRACTION** (forward-ticket candidate from Section D): refactor `render_and_encode_ffmpeg` into `RenderJobSink` interface in `include/chronon3d/timeline/` for cleaner dispatch coupling.
- [ ] **TICKET-DRYRUN-CANONICALIZATION** (forward-ticket candidate from Section D): JSON-format dry-run output for tooling (`jq`-queryable consumption).
- [ ] **TICKET-PREFLIGHT-INTEGRATION** (cross-link, already registered): canonicalize `AssetPreflightResolver` + `PreflightMode` across all entry points; this ticket integrates sub-bullet (e) explicitly.

## Criteri di accettazione (this commit — chaser-cycle ticket-open)

- [x] Subject envelope ≤ 72 chars: `chore(docs): open TICKET-V3-CLI-UNIFICATION-VIDEO-MODE` (52 chars).
- [x] Cat-5 3-doc same-atomic: NEW ticket-home + EDIT `docs/FOLLOWUP_TICKETS.md` §Open Blockers row + EDIT `docs/CHANGELOG.md` cite-only entry.
- [x] Cat-3 minimal-surface: ZERO source touched (Phase 0 = design-only chaser-ticket).
- [x] Cat-2 freeze compliant: zero new SDK symbol proposed in design (extensions are sub-struct fields; ABI-stable).
- [x] Forward-points grep-discoverable: 2 P2 forward-tickets (REMOVE-VIDEO + PREFLIGHT-INTEGRATION) cross-referenced in §Forward-points.
- [x] All 7 design questions in Section A answered.
- [x] 5-phase migration plan in Section B (Phase 0-4) with explicit macchina-verifica paths per phase.
- [x] 5-entry risk register in Section C (probability×impact-ranked, each with mitigation + gate).
- [x] 3 forward-tickets identified in Section D.
- [x] Subject envelope verified ≤ 72 per `tools/check_commit_subject_length.sh`.

## macchina-verifica (DEFERRED-WBH per VCPKG env-block)

This VPS lacks vcpkg glm/magic_enum env (`TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` blocker + `TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX` downstream rot). `cmake --build build/dev_fast` non-reproducible for `chronon3d_cli_core` target. Design-session verify deferred to working build host.

VPS-only verification (this session):
- `rg -nc 'TICKET-V3-CLI-UNIFICATION-VIDEO-MODE' docs/` returns ≥ 5 matches (FOLLOWUP_TICKETS row + CHANGELOG cite-only + this ticket-home + parent ticket-home cross-link + 1 sibling cross-link).
- `rg -nc 'RenderMode::Video' include/chronon3d/timeline/render_job.hpp apps/chronon3d_cli/utils/job/render_job.hpp apps/chronon3d_cli/commands/render/command_render.cpp apps/chronon3d_cli/commands/video/command_video.cpp` returns ≥ 4 matches (canonical descriptor + canonical dispatcher + video sub-system integrate).
- `bash tools/check_main_clean.sh` → GATE_PASS.

## Cross-link (catena Audit §13 macro-chore)

- AGENTS.md §`### Docs canonical update discipline rule` (Cat-3 anti-dup codification — this ticket-home is the cronaca host for VIDEO-MODE).
- AGENTS.md §`### 2×-in-one-chore: deprecation reversal bundles forward-point tickets` (forward-points bundled atomically; the 5 sub-bullets (a)-(e) inherited from `TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3` §Forward-points via fold Cat-3 anti-dup integrity closure).
- AGENTS.md `### Fasi 1-4 — Test coverage expansion` (planned V0.2 milestone per `docs/FOLLOWUP_TICKETS.md` §Open Blockers prior context).
- AGENTS.md §Post-push SHA-selfcheck invariant (SHA-triple verify post-push).
- AGENTS.md §Cat-2 freeze (no new SDK symbol across all 4 implementation phases).
- AGENTS.md §Cat-3 minimal-surface (zero source touched in this chaser-cycle ticket-open; minimal-surface per phase in subsequent implementation commits).
- AGENTS.md §regole "fare PR piccole" (5-phase plan ensures each phase is a minimum-scope atomic chore).
- C1 (commit `b47c8b85 feat(render): D1 - unified RenderJob covering still, sequence, and video modes`, 2026-07-10): provides the canonical `RenderJob` descriptor; this ticket extends the EXECUTOR fold (not the descriptor shape).
- Strategy Y (audit §17+§12 finale commit `70782118c`, 2026-07-14): pre-condition SHIPPED.
- Forward-points fold chore (commit `37cd407d7`, 2026-07-14): this ticket's 5 sub-bullets (a)-(e) inherited.
- Aliases Phase-3 closure (commit `2e1e3201f`, 2026-07-14): alias TTL pre-condition PREREQ.
- Word-literal-compliance chaser-chore (commit `9318f8124`, 2026-07-14): audit §13 verbatim warning form.
- Citation-truth chaser-chore (commit `69ee6456`, 2026-07-14): HONEST-discipline verify.
- Parent ticket `docs/tickets/TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3.md` (DONE-WITH-WORD-LITERAL-COMPLIANCE): §Forward-points VIDEO-MODE sub-bullets (a)-(e) are the source of truth for this ticket's scope.
- Sibling forward-ticket `docs/tickets/TICKET-PREFLIGHT-INTEGRATION.md` (forward-point): AssetPreflightResolver canonicalization; this ticket integrates sub-bullet (e) explicitly.
- Sibling forward-ticket `docs/tickets/TICKET-V3-CLI-UNIFICATION-REMOVE-VIDEO.md` (forward-point): delete `command_video` etc post-Phase-5.
- Sibling forward-ticket `docs/tickets/TICKET-V3-CLI-UNIFICATION-REMOVE-STILL.md` (forward-point): delete `command_still` etc in V0.2.

## Cronaca estesa (catena Audit §13 expansion)

Per AGENTS.md Cat-3 anti-dup, the full design-session cronaca lives here (Section A/B/C/D above). `docs/CHANGELOG.md` entry + `docs/FOLLOWUP_TICKETS.md` row are cite-only (1-line summary + ticket-home link).
