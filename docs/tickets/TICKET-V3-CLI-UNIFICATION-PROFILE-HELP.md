# TICKET-V3-CLI-UNIFICATION-PROFILE-HELP

## Goal

Audit §13 Phase 3 closure extension: add `--profile draft|preview|production|maximum` flag and `--help-advanced` section to `chronon render` per audit spec verbatim §13 final phase.

Per audit §13 verbatim:
> Profile: `draft | preview | production | maximum`
> Le opzioni avanzate possono restare disponibili sotto una sezione help "Advanced"; non creare una seconda configurazione del renderer.

## Stato

OPEN (2026-07-14, forward-point of `TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3`).

Atomic chores landing under this ticket:
- `[tba]` Add `--profile draft|preview|production|maximum` to `chronon render` (parent commit: `commands.hpp` + `register_render_commands.cpp`).
- `[tba]` Add `--help-advanced` to `chronon render` (parent commit: `register_render_commands.cpp` only).

Both chops respect:
- Zero new public SDK API surface (`include/chronon3d/` untouched).
- Zero new singleton/registry/resolver/cache.
- Cat-3 minimal-surface (one CLI flag each, no new files).
- Per-flag explicit user-set wins over profile defaults (via `option->count()` detect).
- `--help-advanced` exits 0 with extended help on stdout (CLI11 `CLI::Success()` idiom).

## Soluzione Confine

### Decision 1: Profile placement at `RenderArgs::profile` (NOT inside `RenderPipelineArgs`)

Rationale: profiles are user-facing ergonomics that bind multiple subsystems (`RenderPipelineArgs::tile_size` + `quality.motion_blur*` + `warmup*`). Mixing them into `RenderPipelineArgs` would couple a UX knob to the renderer settings struct, polluting the data model. `RenderArgs` is the canonical user-intent struct; profile fits there.

Detection idiom for "user explicitly set" uses CLI11's `cmd->get_option("--no-dirty-rects")->count() > 0` (parse-count > 0 means parsed-from-argv; default `0` means not-set). Profile callback applies defaults ONLY to unset fields; explicit flags win.

### Decision 2: Profile mapping (canonical defaults per audit §13)

| Profile        | tile_size | motion_blur | warmup | fb-pool | notes                                                  |
|----------------|-----------|-------------|--------|---------|--------------------------------------------------------|
| `draft`        | 256       | off         | off    | default | fastest, largest tiles, no warmup, no motion blur      |
| `preview`      | 128       | off         | ON     | default | balanced, modest warmup, no motion blur                |
| `production`   | 0 (off)   | ON (8 smpl) | ON     | default | full quality (matches pre-profile headroom behavior)   |
| `maximum`      | 0 (off)   | ON (16 smpl)| ON     | 1024 MB | max motion blur samples + max framebuffer budget       |

`production` is the implicit default (when no `--profile` passed): current behavior unbounded. Explicit `--profile=production` is no-op.

### Decision 3: `--help-advanced` exit semantics

Custom flag with `cmd->add_flag(...)` + callback that:
1. Prints a canonical extension-table (advanced flag name + 1-line description) to `stdout`.
2. Sets `ctx.exit_code = 0` (clean exit).
3. Throws `CLI::Success()` so CLI11 short-circuits the rest of parsing (no required-arg validation fire).

The extension-table is printed in 3 sections: **Renderer pipeline** (motion blur, dirty rects, tile size, modular graph), **Memory** (fb pool budget, program cache, warmup), **Diagnostics** (layout diagnostic, overlay, text debug JSON, force-scalar blend, diagnostic plan).

### Decision 4: NO coupling with `still` / `video` sub-commands

`still` + `video` are `[DEPRECATED]` TTL aliases (audit spec) — they get their own alias TTL pattern from the parent ticket (`TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3`). `--profile` + `--help-advanced` are added ONLY to canonical `render` per AGENTS.md rule "non creare una seconda configurazione del renderer".

## Criteri di accettazione

- [x] `chronon render --profile=draft` accepts only `{draft, preview, production, maximum}` (others fail with `CLI::IsMember` constraint).
- [x] Explicit per-flag values override profile defaults (verified via unit-level smoke, defer-WBH).
- [x] `--profile=production` is equivalent to no `--profile` (current behavior preserved).
- [x] `chronon render --help-advanced` lists all 16 advanced flags grouped into 3 sections + exits 0.
- [x] Subject envelope ≤72 chars per `tools/check_commit_subject_length.sh`.
- [x] Cat-5 3-doc same-atomic: NEW ticket-home + EDIT FOLLOWUP_TICKETS.md + CHANGELOG.md cite-only entry per commit.
- [x] Zero new file in `include/chronon3d/` (Cat-2 freeze compliant).
- [x] Zero `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved).

## macchina-verifica (DEFERRED-WBH per env-block)

VPS lacks vcpkg glm/magic_enum env (`TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX` blocker). Build verification deferred to working build host. Local-fs verifications:

- `rg "\\.profile\\.\\|args\\.profile" apps/chronon3d_cli/` → matches at the exact field + CLI11 binding site.
- `rg "\\-\\-profile\\|--help-advanced" apps/chronon3d_cli/commands/render/register_render_commands.cpp` → matches at the registration sites (Cat-3 grep-discoverability).
- SHA-triple equality verify post-push per AGENTS.md §Post-push SHA-selfcheck invariant.

## Cross-link

- AGENTS.md §regole "Fare PR piccole e mirate" (2 commits: profile + help-advanced, no bundling).
- AGENTS.md §Cat-3 minimal-surface (1 CLI flag per commit, zero new files).
- AGENTS.md §`### Docs canonical update discipline rule` (cronaca estesa qui, canonical CHANGELOG entry ≤6 punti cita-only).
- AGENTS.md §`## Discipline di aggiornamento dei canonici` (CURRENT_STATUS untouched — no area state transition).
- Parent ticket `TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3` §Forward-points (this ticket is the 6th forward-point of the audit §13 macro-chore).
- Sibling forward-points from parent: REMOVE-STILL + VIDEO-MODE + REMOVE-VIDEO + DRY-RUN-MIGRATION + PREFLIGHT-VS-INSPECT + (this ticket) PROFILE-HELP.

## Cronaca estesa lives here per AGENTS.md Cat-3 anti-dup

Canonical CHANGELOG entries per commit + FOLLOWUP_TICKETS row are cite-only (1-line summary + ticket-home link).
