# TICKET-AE-LIKE-TEXT-ACTION-PLAN — Strategy to AE-like kinetic typography 2D (land on `main`, doc-only)

| Field | Value |
|---|---|
| **Status** | PLANNED (umbrella strategy file; landed doc-only on `main` as a guide for the rollout; not a single deliverable) |
| **Date** | 2026-07-07 |
| **Deciders** | Agent 3 (text subsystem strategy + AE-parity rollout coordination) |
| **Tags** | `text`, `kinetic-typography`, `AE-parity-2.0`, `after-effects-parity`, `Wiggly`, `Wave`, `Expression-Selector`, `Character-Offset`, `per-character-3D`, `subtitle-timed`, `feature-freeze-V0.1-revoked` |
| **Related** | [docs/ROADMAP.md §M1.6 — AE-Parity Cinematic Text Golden Expansion](../ROADMAP.md) (parent workstream envelope); [docs/adr/ADR-014-text-golden-coverage.md](../adr/ADR-014-text-golden-coverage.md) (the AGENTS.md v0.1 Cat-5 freeze-compliant doc-only precedent — 12 user-spec tests + 5 forward-only rows); [docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md](../TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md) (the long-form canon; Blocco 6 selector engine + Blocco 10 subtitle word timing + Blocco 11 advanced features); [docs/FOLLOWUP_TICKETS.md](../FOLLOWUP_TICKETS.md) (the canonical INDEX carrier — every killer row + every CINEMATIC row in §P1 backlog is the live source-of-truth); `tools/wrap_push.sh` (GATE-MNT-01 push-side wrapper that lands each rollout commit atomically on `main`); `tools/check_doc_sync.sh` gate #7 (every rollout commit MUST touch the 4 canonical docs in lockstep). |

---

## Context and scope

Chronon3D's text subsystem already ships 22 built-in presets, a canonical TextDocument + TextRunLayout + TextRunShape pipeline, FreeType+HarfBuzz+FriBidi shaping, multi-line layout, Range Selector for Glyph/Grapheme/Character/Word/Line, 11 animatable per-unit properties (Position/Scale/Rotation/Skew/Anchor/Opacity/Blur/Fill/Stroke/Tracking/Baseline Shift), text on path, scramble+replace+animate, and the AE-parity cinematic suite floor of 7 already-shipped scene builders (`tests/text_golden/ae_parity/ae_0[1-9]*.cpp` × `TICKET-AE-PARITY-CINEMATIC-{01..09}`).

**Empirical state at `main@33568b90` (2026-07-07):**

| Surface                | Current (PASS/PARTIAL/PLANNED)                                                                  | Target                                                                                     | Gap                                                                            |
|------------------------|-------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------|
| Scene-builder `.cpp`   | **7 / 20 IMPL** (`CINEMATIC-01..05 + 06 + 09` DONE; 13 PLANNED in §P1 backlog)                  | **20 / 20** (full M1.6 floor)                                                              | **13 scene builders to land**                                                 |
| Golden PNG AE-parity   | **42 / 288 PNG** (ae_*: 30 + 6 (06) + 6 (09) = 42 PNG floor observable)                         | **288 PNG** (20 × 2 AR × 3 frame = 120 + 20 user_spec + 6 motion_blur + 5 killer evidence) | **246 PNG to capture + commit**                                               |
| Reflexões killerr tests | **3 / 5** placeholder rows (`WIGGLY-WAVE-EXPRESSION`, `PER-CHAR-3D`, `DRIVER`, `FLOOR` + `GOLDEN-30` karaoke)                                                                  | **5 / 5** lockable tests (Expression Selector + Character Offset/Value/Range + Subtitle Word Timing + Wiggly/Wave/Random + Per-Char 3D) | **2 missing tickets** (EXPRESSION-SELECTOR + CHARACTER-OFFSET-VALUE-RANGE seeded in same commit) |
| Referee pipeline       | **NOT RUN** (no `tools/ae_parity_referee.sh` yet)                                              | **PASS on 10/15+ scenes** (anti-greenwashing gate)                                          | **whole workstream to build**                                                 |

**This action plan** formalises the rollout to AE-like kinetic typography 2D through 5 documented Decisions, each with explicit Spec + Source anchor + Test lock + Failure mode. Decisions are sequenced so the rollout can proceed in small atomic steps on `main` (no branches, no mega-PR — per AGENTS.md §"Regole di lavoro" + Marcuss-ops operational rule).

---

## Decision 1 — Three-step rollout envelope: `Scene builders → Killer tests → Floor + Referee`

### Spec

The rollout is structured as three sequential phases, each with a clear PARTIAL→PASS exit gate:

**Phase 1 — Scene-builder IMPL floor (13 builders to close the 7→20 gap)**
- Atomic commit per scene-builder, each adding 1 file under `tests/text_golden/ae_parity/ae_NN_<id>.cpp` + 6 PNG (16:9 + 9:16 × 0/15/30) + a row in `docs/FOLLOWUP_TICKETS.md` flipping PLANNED→DONE.
- Sequence: `ae_07_stroke_reveal` → `ae_08_glow_pulse` → `ae_10_scale_pop` → `ae_11_rotation_per_character` → `ae_12_random_character_jitter` → `ae_13_text_on_path` → `ae_14_multiline_9_16_safezone` → `ae_15_long_paragraph_wrap` → `ae_16_mixed_font_rich_text` → `ae_17_arabic_english_bidi` → `ae_18_small_subtitle_readability` → `ae_19_fast_motion_stress` → `ae_20_karaoke_word_highlight`.
- Each scene uses the canonical pipeline: `composition({...}, lambda)` → `SceneBuilder::layer(...)` → `LayerBuilder::text(...)` / `.text_run(...)` → `verify_golden(*fb, ...)` (helper from `tests/visual/support/golden_test.hpp`).
- ZERO `TextShape` / `rich_text` / legacy `TextAnimator` references (AGENTS.md v0.1 Cat-2 freeze-compliant).

**Phase 2 — Killer test workstream (5 determinism + cross-run + referee lockable tests)**
- 5 atomic commits, one per killer (the 5th is the karaoke killer already tracked via `TICKET-GOLDEN-30`; this ticket cross-links it).
- Each killer: `tests/text_golden/ae_parity_killer/<name>.cpp` (NEW sub-directory under `ae_parity/`) with 3 SUBCASEs (seed-lock + frame-by-frame delta + cross-run reproducibility). Diff asset under `tests/text_golden/ae_parity_killer/reference/`.

**Phase 3 — 288 PNG floor deliverable + `tools/ae_parity_referee.sh`**
- Computed by gap-fill: 13 scene builders × 6 PNG = 78 PNG (closing 42→120 floor) + 20 user_spec (already 20 in `test_renders/golden/text/`) + 6 motion_blur text (TICKET-MOTION-BLUR-TEXT) + 5 killer evidence (5 × 1 reference frame + 1 cross-run = 10 PNG) → **288 PNG floor** verifiable macchina.
- Build `tools/ae_parity_referee.sh`: per scene, `reference/after_effects/scene_NNN_frame_NN.png` (AE-side mock — placeholder for future real AE renders) + `reference/chronon3d/scene_NNN_frame_NN.png` (engine output) + `mean_abs_diff + perceptual color metric` lock at 5/255 threshold (Rigoroso — `docs/FOLLOWUP_TICKETS.md §TICKET-AE-PARITY-DRIVER`).
- Anti-greenwashing: no claim of "AE-like" until referee pipeline exits 0 on at least **10/15** cinematic scenes (PARTIAL=1-9/15; PASS=10+/15).

### Source anchor

- `tests/text_golden/ae_parity/ae_NN_*.cpp` (13 scene-builder files to add; harness extends `chronon3d_text_golden_tests` via `target_sources(...)` appendage)
- `tests/text_golden/ae_parity_killer/<5 killers>.cpp` (NEW sub-cluster; `ae_parity_killer/` registered same way)
- `tests/text_golden/ae_parity_killer/reference/<sha256>.txt` (determinism evidence — same-frame-same-result across serial/parallel/random-access)
- `test_renders/golden/text/ae_<NN>_<id>_*.png` (246 PNG to capture via `CHRONON3D_UPDATE_GOLDENS=1`)
- `tools/ae_parity_referee.sh` (NEW bash driver, sibling to `tools/test_golden_driver.sh`)
- `docs/FOLLOWUP_TICKETS.md` (each rollout commit updates the corresponding ticket row PLANNED→PARTIAL→DONE)

### Test lock

Each commit's test lock is the trio:
1. **build atomic-gate**: `cmake --build build/chronon/linux-fast-dev --target chronon3d_text_golden_tests -j$(nproc)` exit 0
2. **golden verify**: `./build/chronon/linux-fast-dev/tests/chronon3d_text_golden_tests --test-case="ae_<NN>*"` exits 0 with PNG-delta match
3. **doc-sync**: `tools/check_doc_sync.sh` exit 0 (gate #7 — every commit MUST touch one of the 4 canonical files)

### Failure mode (if silently broken)

- **Scene-builder rot (Phase 1)** — a future builder reuses legacy `TextShape` path. `tools/check_architecture_boundaries.sh` Check 14/15 detects the rot (legacy text-pipeline sweep); `git grep -nE '\bTextShape\b' tests/text_golden/ae_parity/*.cpp` should stay at ZERO post-rollout.
- **Killer test determinism rot (Phase 2)** — a future change hashes `system_clock::now()` somewhere in the rendering pipeline, breaking seed-lock SUBCASEs. `git grep -nE 'system_clock|chrono::system_clock' src/text/` should stay at ZERO (only `chrono::steady_clock` for profiling is allowed).
- **Referee rot (Phase 3)** — `tools/ae_parity_referee.sh` silently returns 0 on a missing reference PNG. The bash script MUST `set -euo pipefail` + explicit missing-reference-file guard. Code-reviewer of any PR to the script MUST verify the guard.

---

## Decision 2 — Killer test specs (5 determinism + cross-run corridor tests)

### Spec

Each killer test establishes a corridor that proves the engine is **AE-like in substrate**, not just visually similar to an AE render:

**Killer 1 — Wiggly/Wave/Random Selector** (cross-link `TICKET-AE-PARITY-KILLER-WIGGLY-WAVE-EXPRESSION`)
- Per-glyph random offset via WigglySelector with `min/max amount + wiggles/sec + correlation + lock dimensions + seed`
- Acceptance: same-seed same-frame-same-result across serial + parallel + random-access frame 30; change-seed-different-motion observable
- Asset: `tests/text_golden/ae_parity_killer/killer_01_wiggly_wave.cpp` (3 SUBCASEs)

**Killer 2 — Expression Selector** (NEW ticket `TICKET-AE-PARITY-KILLER-EXPRESSION-SELECTOR` seeded by this commit)
- Evaluate per-character expression `amount = selectorValue * textIndex / textTotal`
- Acceptance: random access frame 30 == sequential rendering up to frame 30 (byte-identical); zero global mutable state accessed from the expression evaluator
- Source anchor: `src/text/selector/expression_evaluator.cpp` (NEW; resolver comes from `AnimatorResolver` per Blocco 6)
- Asset: `tests/text_golden/ae_parity_killer/killer_02_expression_selector.cpp` (3 SUBCASEs)

**Killer 3 — Character Offset / Value / Range** (NEW ticket `TICKET-AE-PARITY-KILLER-CHARACTER-OFFSET-VALUE-RANGE` seeded by this commit)
- Character Offset +5 changes the actual glyph ID (NOT a post-layout visual transform; this is the property-stage invalidation contract from Blocco 2 of the text roadmap)
- Character Value replaces a specific index's text content
- Character Range applies an override to a contiguous range
- Acceptance: pre-shaping invalidation MUST swap glyphs + invalidate shaping cache + invalidate layout cache; post-shaping-only changes pass the lock contract from `tests/text/test_text_property_stage_invalidation.cpp`
- Source anchor: `src/text/source/character_offset_evaluator.cpp` (NEW; replaces the current `GlyphInstanceState`-only path)
- Asset: `tests/text_golden/ae_parity_killer/killer_03_character_offset.cpp` (3 SUBCASEs)

**Killer 4 — Per-character 3D** (cross-link `TICKET-AE-PARITY-KILLER-PER-CHAR-3D` + `TICKET-AE-PARITY-CINEMATIC-11`)
- Each letter rotates on Y-axis; camera perspective active; Z ordering correct; no collapse to 2D
- Acceptance: frame 0 ≠ frame 30 (proves 3D depth actually renders, not a 2D rotation); pixel-changed-ratio > 0.10 at frame 15
- Asset: `tests/text_golden/ae_parity_killer/killer_04_per_char_3d.cpp` (3 SUBCASEs)

**Killer 5 — Subtitle Word Timing** (cross-link `TICKET-GOLDEN-30` karaoke + `TICKET-AE-PARITY-CINEMATIC-20`)
- Per-word SRT/VTT/JSON adapter ingest; active word fills + scales; safe-area respected
- Acceptance: random-access frame 120 == sequential rendering up to frame 120; no audio-time / frame-time drift
- Source anchor: `src/text/timed_text/semantic_word_highlight.cpp` (NEW; uses existing `timed_text_adapter_srt.cpp` + the `WordHighlightProperty` spec from Blocco 10)
- Asset: `tests/text_golden/ae_parity_killer/killer_05_subtitle_word_timing.cpp` (3 SUBCASEs)

### Source anchor

- `docs/adr/ADR-014-text-golden-coverage.md` (the AGENTS precedent for KILLER-style test scaffolding)
- `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md Blocco 6 + Blocco 10 + Blocco 11` (the semantic-grounding each killer is built atop)
- `tests/visual/support/golden_test.hpp` (`verify_golden` helper, ENV-gated Update mode)
- `tests/visual/support/image_diff.cpp` (per-pixel diff for the `mean_abs_diff 5/255` lock)
- `tools/test_golden_driver.sh` (template for `ae_parity_referee.sh`)

### Test lock

Each killer's 3 SUBCASEs lock ONE deceptively-simple property that proves the engine has the AE-like substrate — seed determinism + cross-run reproducibility + frame-random-access byte-identity. The lock is intentionally tight (no "approximate match" fallback) so any future regression to "AE-like visually but not in substrate" is detected.

### Failure mode (if silently broken)

- **Killer rot** — a future "near-AE" feature downgrade (e.g. Wiggly becomes "deterministic but only at serial run", killer 1 SUBCASE 3 cross-run reproducibility silently breaks). The 3-SUBCASE pattern per killer catches this.
- **Selector cross-contamination** — two selectors leaking through `static` global state on parallel runs. `git grep -nE 'static\s+\w+\s+\w+_cache_' src/text/selector/` should stay at ZERO (selectors MUST be reentrant).

---

## Decision 3 — AGENTS doc-slot discipline for the rollout

### Spec

Every rollout commit touches the 4 canonical docs in lockstep per AGENTS.md `tools/check_doc_sync.sh` gate #7:

| Slot                        | Allowed                                                                                                                                    | Forbidden                                                                                       |
|-----------------------------|---------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------|
| `docs/CURRENT_STATUS.md`    | Single-line PASS/PARTIAL/DONE row flip + evidence (`SHA + commit` chain)                                                                     | Percentage estimates, "looks good", unbounded prose                                                  |
| `docs/ROADMAP.md`           | `M1.6 §Cinematic Text Golden Expansion` row updates; milestone DoD advancement                                                              | Renaming the section (would invalidate cross-link); adding freeform "plans" without milestone DoD |
| `docs/RELEASE_GATE.md`      | Acceptance criteria advancement                                                                                                              | Adding freeform scope (would invalidate gate count)                                                |
| `docs/FOLLOWUP_TICKETS.md`  | Single ticket row flip (`State: PLANNED → PARTIAL → DONE`) + `commit_sha` cross-link                                                        | Multi-ticket row consolidation (one row per ID — atomic-per-responsibility)                         |
| `docs/tickets/TICKET-NNN.md`| Sub-cluster ticket deep-dive (e.g. this file)                                                                                                | New "status report"-named files (`docs/STATUS.md`, `docs/PLAN.md`, `docs/ACTION_PLAN.md`)            |
| `docs/adr/ADR-NNN-*.md`     | Architectural decision; one file per decision; cross-link existing tickets (NOT vice-versa)                                                  | Implementation status / timeline / partial partial                                                  |

This is **Decision 1 of this file's own structure**: the rollout NEVER creates `docs/PLAN.md`, `docs/STATUS.md`, `docs/STATUS_NEXT.md`, `docs/ACTION_PLAN.md`, `docs/ROADMAP_*.md` variants, or `docs/FOLLOWUP_*.md` variants — all progress lives in the canonical carriers (per AGENTS.md §"Pattern di filename vietati").

### Source anchor

- `AGENTS.md` §"Pattern di filename vietati" (canonical pattern rejection list)
- `AGENTS.md` §"Regola del gate doc-sync" (gate #7 enforcement)
- `tools/check_doc_sync.sh` (script-of-truth — allowlist of 4 canonical files)

### Test lock

The `tools/check_doc_sync.sh` script is the per-commit lock. Any commit introducing a forbidden filename pattern → script exits ≠0 → `tools/wrap_push.sh` (GATE-MNT-01) refuses the push.

### Failure mode (if silently broken)

- **Drift detection rot** — `tools/check_doc_sync.sh` is locally bypassed by an agent pushing to `main` directly. Wrap_push integrates the gate; bypassing the wrapper means missing GATE-MNT-01.
- **Section split rot** — a future PR splits `docs/ROADMAP.md §M1.6` into `docs/M1.6-cinematic.md` + `docs/M1.6-killers.md` + `docs/M1.6-floor.md`. AGENTS.md explicitly forbids roadmap variants: any new file matching `docs/ROADMAP_*.md` triggers the gate.

---

## Sequenced rollout (operational ready-order)

The following enumeration is the EXECUTABLE ready-order. Each item is one atomic commit on `main`. The exact commit order is a sequencing decision; this list is intended to be iterated in roughly this order, but is NOT a hard serialization (concurrent work on different Phase-1 scene-builders in non-overlapping files is fine).

### Phase 1 — Scene-builder IMPL (13 atomic commits: 7→20)

```
[TICKET-AE-PARITY-CINEMATIC-07] ae_stroke_reveal        (stroke animates first, fill composes after — paint-order determinism)
[TICKET-AE-PARITY-CINEMATIC-08] ae_glow_pulse            (animated glow without burning legibility)
[TICKET-AE-PARITY-CINEMATIC-10] ae_scale_pop            (text enters with overshoot — spring curve)
[TICKET-AE-PARITY-CINEMATIC-11] ae_rotation_per_character  (prerequisite parziale per Killer 4)
[TICKET-AE-PARITY-CINEMATIC-12] ae_random_character_jitter (determinism via seed)
[TICKET-AE-PARITY-CINEMATIC-13] ae_text_on_path         (cross-link TICKET-GOLDEN-32)
[TICKET-AE-PARITY-CINEMATIC-14] ae_multiline_9_16_safezone (TikTok/Reels safe-area)
[TICKET-AE-PARITY-CINEMATIC-15] ae_long_paragraph_wrap   (long text + narrow box wrap determinismo)
[TICKET-AE-PARITY-CINEMATIC-16] ae_mixed_font_rich_text  (riga con bold + small + large + colored spans)
[TICKET-AE-PARITY-CINEMATIC-17] ae_arabic_english_bidi   (FriBidi cross-link CHRONON3D_FORCE_NO_FRIBIDI)
[TICKET-AE-PARITY-CINEMATIC-18] ae_small_subtitle_readability (readability lock)
[TICKET-AE-PARITY-CINEMATIC-19] ae_fast_motion_stress   (motion blur trigger + antialias)
[TICKET-AE-PARITY-CINEMATIC-20] ae_karaoke_word_highlight (cross-link TICKET-GOLDEN-30)
```

### Phase 2 — Killer tests (5 atomic commits)

```
[KILLER-WIGGLY-WAVE-EXPRESSION]   killer_01_wiggly_wave           (decision: WigglySelector implementation folds +3 lock SUBCASEs)
[KILLER-EXPRESSION-SELECTOR]      killer_02_expression_selector    (NEW ticket seeded in this commit)
[KILLER-CHARACTER-OFFSET-VALUE-RANGE] killer_03_character_offset  (NEW ticket seeded in this commit)
[KILLER-PER-CHAR-3D]              killer_04_per_char_3d            (cross-link CINEMATIC-11)
[KILLER-SUBTITLE-WORD-TIMING]     killer_05_subtitle_word_timing  (cross-link TICKET-GOLDEN-30)
```

### Phase 3 — Floor + Referee (2 atomic commits)

```
[TICKET-AE-PARITY-FLOOR]    288 PNG tracked in test_renders/golden/text/   (gap 42→288)
[TICKET-AE-PARITY-DRIVER]   tools/ae_parity_referee.sh exit 0 on 10/15+ cinematic scene
```

### Cross-cutting gates (must hold at the end of each phase)

```
[tools/check_doc_sync.sh]              exit 0  (doc drift caught)
[tools/check_architecture_boundaries.sh] exit 0  (16/16 PASS incluso Check 14/15 legacy)
[tools/check_legacy_text_pipeline.sh]  exit 0  (3/3 PASS)
[tools/wrap_push.sh origin main]       exit 0  (GATE-MNT-01 + per-branch rebase)
[docs/baselines/...]                   new macchina-verified baseline documented
[chronon3d_text_golden_tests]          PASS on all `ae_*` TEST_CASE
```

---

## Done-Definition for the entire rollout

The rollout is **DONE** when, on the same `main` commit:

1. **20/20 scene builders IMPL** + `chronon3d_text_golden_tests --test-case="ae_*"` exits 0
2. **288/288 PNG tracked** in `test_renders/golden/text/ae_*` (gitignored-friendly, whitelisted; `git ls-files HEAD ./test_renders/golden/text/ | wc -l ≥ 288`)
3. **5/5 killer tests PASS** + the 3-SUBCASE pattern locked per killer
4. **`tools/ae_parity_referee.sh` 10/15+ PARTIAL/PASS** on the cinematic scenes (NOT RUN=0/15; PARTIAL=1-9/15; PASS=10+/15)
5. **`docs/FOLLOWUP_TICKETS.md` rows flip DONE** for every rolled ticket
6. **`docs/CURRENT_STATUS.md` row PASS** for "AE-parity v2.0" line (alongside camera V1 / text V1 / SDK V1)
7. **`docs/CHANGELOG.md` M1.6 closing entry** documenting macchina-verified baseline SHA

---

## Consequences

### Positive

- **Struttura unica rollout** — this ticket IS the SSOT for AE-like text rollout, replacing ad-hoc ADR/sticky-note/playbook drifts
- **Anti-duplication** — atomic-per-responsibility commits prevent the "Phase E action-plan landing + Phase E action-plan closing 1 week later" pattern from leaking into the AE roll
- **AGENTS.md v0.1 Cat-5 compliant** — this file lives in `docs/tickets/` (canonical "Scheda ticket specifico" slot), not in any forbidden variant
- **Cross-rollout traceability** — each of the 13+5+2 tickets has a single row in `docs/FOLLOWUP_TICKETS.md` plus a single deep-dive file (or this umbrella)
- **Anti-greenwashing** — `tools/ae_parity_referee.sh` PARTIAL=10+ cinches "AE-like" claim to machine-verified evidence, not vibes

### Negative / Migration cost

- **18+ atomic commits** to land before this ticket is DONE — explicit cadence; no mega-PR allowed
- **2 missing ticket-row additions** to `docs/FOLLOWUP_TICKETS.md` (KILLER-EXPRESSION-SELECTOR + KILLER-CHARACTER-OFFSET-VALUE-RANGE) seeded in the same commit as this file — small doc drift if the seed commit ships without the umbrella
- **First-run kinetics**: the 288 PNG capture requires `CHRONON3D_UPDATE_GOLDENS=1` to materialise the SHAs; macchina-verified baseline at HOME is a £~30m capture session

### Neutral

- No source-code changes outside `tests/text_golden/ae_parity/` + `tests/text_golden/ae_parity_killer/` + the underlying selector implementations (Phase 2)
- No new public API surface (all new code is `tests/` + optional internal `src/text/selector/` + `src/text/timed_text/`)
- No new SDK headers in `include/chronon3d/`
- Zero new singleton/registry/resolver/cache

---

## Alternatives considered

- **A. One mega-PR landing all 13 scene builders + 5 killers + 288 PNG + referee in a single commit.** Rejected — AGENTS.md §"Regole di lavoro" + Marcuss-ops operational rule both forbid mega-PRs; the granularity is the value, not the bundling.
- **B. Distribute the rollout across branched feature PRs first, then merge to `main`.** Rejected — AGENTS.md §"Vincoli architetturali di esecuzione" mandates `un commit per ticket` + `direttamente su main, no branch`; features accumulate on `main` one-at-a-time.
- **C. Reuse the existing `ae_01_cinematic_title_reveal` template for all 13 builders.** Partially accepted — the harness pattern (`composition` + `SceneBuilder::layer` + `verify_golden`) is canonical and reused; the per-scene motion-chain compositions are bespoke.
- **D. Skip the referee pipeline and use visual eyeballing for AE-likeness.** Rejected — `docs/FOLLOWUP_TICKETS.md §TICKET-AE-PARITY-DRIVER` and AGENTS.md §"Anti-greenwashing" explicitly mandate `mean_abs_diff` 5/255 threshold; "AE-like" is a claim that needs machine verification
- **E. Add a NEW ADR for the rollout.** Rejected — this ticket + the existing M1.6 § in `docs/ROADMAP.md` + `ADR-014` precedent already cover the doc slots; another ADR would be ceremonial overhead without semantic value.

---

## References

- `AGENTS.md` §"Insieme canonico della documentazione" + §"Pattern di filename vietati" (canonical docs + filename allowlist)
- `AGENTS.md` §"Regole di lavoro" + §"Vincoli architetturali di esecuzione" (one commit per ticket + no branch + main atomically)
- `docs/ROADMAP.md §M1.6 — AE-Parity Cinematic Text Golden Expansion` (the parent milestone envelope)
- `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md §Blocco 5 — Visual Regression Harness` + §Blocco 6 — Selector Engine + §Blocco 10 — Subtitle Word Timing + §Blocco 11 — Funzioni avanzate
- `docs/adr/ADR-014-text-golden-coverage.md` (the AGENTS Cat-5 freeze-compliant doc-only precedent for this ticket structure)
- `docs/FOLLOWUP_TICKETS.md` (canonical index — 13 CINEMATIC-NN PLANNED rows + 4 KILLER-NN PLANNED rows + this commit's 2 NEW KILLER rows; PARTIAL/DONE advances propagate)
- `docs/CHANGELOG.md` (macchina-verified original-baseline source for status-of-record)
- `tools/check_doc_sync.sh` (gate #7 doc-sync enforcement)
- `tools/check_architecture_boundaries.sh` (gate #5 arch-boundary, includes Check 14/15 legacy text-pipeline sweep)
- `tools/check_legacy_text_pipeline.sh` (3/3 PASS — `rasterize_text_to_bl_image` + `TextLayoutEngine::layout` + `text/rich_text.hpp` sweep)
- `tools/wrap_push.sh` (GATE-MNT-01 push-side wrapper that lands each rollout commit atomically + auto-FF + per-branch rebase invariant)
- `tests/text_golden/ae_parity/` (Existing 7 scene builders — ae_01..05 + 06 + 09)
- `tests/text_golden/ae_parity_killer/` (NEW sub-directory — 5 killers)
- `tests/visual/support/golden_test.hpp` (`verify_golden` helper)
- `tests/visual/support/image_diff.cpp` (`mean_abs_diff 5/255` lock)
- `tools/test_golden_driver.sh` (template for `tools/ae_parity_referee.sh`)
