# TICKET-AE-PARITY-DRIVER — AE-parity referee pipeline

| Field | Value |
|---|---|
| **Status** | DONE (2026-07-07, commit (this commit)) |
| **Date** | 2026-07-07 |
| **Deciders** | Agent 3 (AE-parity cluster) |
| **Tags** | `tools`, `AE-parity`, `referee`, `anti-greenwashing`, `cat-5`, `feature-freeze-V0.1-revoked` |
| **Related** | [docs/tickets/TICKET-AE-LIKE-TEXT-ACTION-PLAN.md](TICKET-AE-LIKE-TEXT-ACTION-PLAN.md) (umbrella strategy, Phase 3 referee spec); [docs/FOLLOWUP_TICKETS.md](../FOLLOWUP_TICKETS.md) (canonical index — DONE row with `(this commit)` SHA placeholder); [tools/test_golden_driver.sh](../../tools/test_golden_driver.sh) (sibling bash driver, ADR-014 pattern); [tools/ae_parity_referee.sh](../../tools/ae_parity_referee.sh) (NEW bash driver); [tools/ae_parity_referee/diff_pixels.py](../../tools/ae_parity_referee/diff_pixels.py) (NEW Python+Pillow helper); [tests/visual/support/image_diff.cpp](../visual/support/image_diff.cpp) (canonical C++ image diff impl, formula parity reference); [tests/visual/support/image_diff.hpp](../visual/support/image_diff.hpp) (ImageDiffThreshold + ImageDiffResult structs); `reference/README.md` (NEW scaffold contract doc); TICKET-AE-PARITY-SUITE (the 20 cinematic scenes that will fill `reference/after_effects/`); TICKET-AE-PARITY-FLOOR (288+ PNG floor deliverable); `tools/wrap_push.sh` (GATE-MNT-01); `tools/check_doc_sync.sh` (gate #7) |

---

## Context

Phase 3 of the AE-like kinetic typography 2D rollout (per
`docs/tickets/TICKET-AE-LIKE-TEXT-ACTION-PLAN.md` Phase 3 referee spec):
the **anti-greenwashing gate** that prevents any "AE-like" claim
until a referee pipeline objectively compares engine output to
AE-side reference mocks.  The gate is the *last* layer of the
cat-2 rigor (anti-greenwashing) defense-in-depth: per-scene
visual-goldens (already locked in TICKET-AE-PARITY-CINEMATIC-NN
siblings) + per-property killers (Killer 1-3 locked) + per-scene
referee (this ticket).

The referee compares per cinematic scene:
- `reference/after_effects/scene_NNN_frame_NN.png` (AE-side mock,
  manually committed) — the canonical reference.
- `reference/chronon3d/scene_NNN_frame_NN.png` (engine output,
  gitignored, regenerated per ctest run) — what we are testing.

Per frame: `MAE < 5/255` (mean absolute error) AND `PSNR > 30dB`
(perceptual color metric).  Anti-greenwashing gate: exit 0 ONLY
if `>= AE_PARITY_MIN_PASS` scenes PASS (default 10/15).

---

## Decision 1 — Bash driver sibling to `tools/test_golden_driver.sh`

### Spec

`tools/ae_parity_referee.sh` is a pure-bash orchestrator following
the established `tools/test_golden_driver.sh` pattern (ADR-014):

- `set -euo pipefail` + `log()`/`err()` helpers.
- 3 subcommands: `run` (default) | `scaffold` | `--json` | `--help`.
- `scaffold` is idempotent (creates `reference/` dirs + README +
  `.gitkeep` files) so first-time setup is a single command.
- Precondition checks: python3 + Pillow available; AE + engine
  dirs exist; helper script present.  Clear "fix:" hints on each
  failure.
- Discovery: scan `reference/after_effects/` for
  `scene_NNN_frame_NN.png` pattern, extract `NNN` (scene) and
  `NN` (frame) via bash regex, build associative array of
  `scene -> "frame1 frame2 ..."`.
- Per-frame: invoke Python helper, parse JSON via Python (no
  `jq` dependency), apply MAE + PSNR + blank-guard thresholds.
- Per-scene aggregate: scene PASSES only if ALL frames PASS.
- Exit codes: 0 = gate cleared, 1 = gate blocked, 2 = preconditions
  not met (distinct from 1 so CI can differentiate "no refs" from
  "refs FAILed").
- `--json` mode: emits a single-line JSON summary for CI integration
  (with `verdict: "PASS"|"FAIL"` + per-bucket counts + thresholds).

### Source anchor

- `tools/ae_parity_referee.sh` (NEW, ~250 LOC, sibling to
  `tools/test_golden_driver.sh`).
- `tools/test_golden_driver.sh` (sibling pattern reference).
- `tools/ae_parity_referee/diff_pixels.py` (NEW, ~110 LOC, the
  actual pixel-diff math).

### Test lock (bash-level, not ctest)

- `bash -n tools/ae_parity_referee.sh` exit 0 (syntax check).
- `python3 -c "import py_compile; py_compile.compile(...)"` exit 0
  (syntax check on helper).
- `tools/ae_parity_referee.sh --help` exit 0 (usage printed).
- `tools/ae_parity_referee.sh scaffold` exit 0 (creates
  `reference/after_effects/` + `reference/chronon3d/` +
  `reference/README.md` + 2 `.gitkeep`).
- `tools/ae_parity_referee.sh` with empty `reference/after_effects/`
  exit 1 with clear "no scene_NNN_frame_NN.png found" message.
- `tools/ae_parity_referee.sh --json` with empty refs exit 1
  with JSON output (machine-readable for CI).
- `tools/ae_parity_referee.sh` with all-zero PNGs in both dirs
  exit 1 (blank-frame anti-greenwashing guard fires).

### Failure mode (if silently broken)

- **No `set -euo pipefail`**: a failed precondition would silently
  propagate, masking real errors.  Hard-locked.
- **No scaffold subcommand**: first-time setup would require manual
  mkdir + README, drift-prone.  Locked in script.
- **No `AE_PARITY_*` env-var overrides**: CI could not tighten the
  gate per-PR.  All 3 thresholds env-overridable.
- **JSON output missing**: CI integration would require parsing the
  human-readable table (brittle).  `--json` flag locks CI parity.
- **Blank-frame greenwashing**: two pure-black PNGs would yield
  MAE=0 + PSNR=inf, falsely PASSing.  Locked at
  `diff_pixels.py::compute_diff()` via `blank` flag (ref + eng
  pixel sum == 0).

---

## Decision 2 — Python+Pillow pixel-diff helper with formula parity

### Spec

`tools/ae_parity_referee/diff_pixels.py` is a small Python helper
using Pillow (already in dev env, version >= 10 tested with 11.3.0)
to compute MAE + PSNR.  ImageMagick is intentionally NOT a dependency
(not installed in CI).  Pure-stdlib + Pillow only.

Formula parity with `tests/visual/support/image_diff.cpp` (the
canonical C++ image-diff used by ctest goldens):

- `MAE  = (Σ |ΔR|+|ΔG|+|ΔB|) / (W * H * 3)`     [units: 0..255]
- `MSE  = (Σ (ΔR)²+(ΔG)²+(ΔB)²) / (W * H * 3)` [units: 0..65025]
- `PSNR = 10 * log10(255² / MSE)`                [units: dB]
  (special case: MSE=0 ⇒ PSNR=inf)

Single-pass accumulation: MAE numerator + MSE numerator + content
sum (anti-greenwashing blank-frame guard).  No `numpy`/`scipy`
dependency — Pillow `tobytes()` gives a flat `bytes` object that
zips into a tight Python loop on the order of `W*H*3` iterations
(1080p = ~6.2M iterations, completes in <100ms on modern hardware).

Per-file guards:
- Pre-decode size guard: `< 256 bytes` ⇒ `ref_too_small` /
  `eng_too_small` (catches placeholder/header-only PNGs).
- Decode error guard: `PIL.UnidentifiedImageError` / `OSError`
  caught, returns `{error: "ref_decode"|"eng_decode"}`.
- Size-mismatch guard: `ref.size != eng.size` ⇒
  `{error: "size_mismatch", ref_size, eng_size}` (referee must
  render the engine output at the same resolution as the AE mock).
- Blank-frame guard: `ref_pixel_sum == 0 AND eng_pixel_sum == 0`
  ⇒ `blank: true` (caller = bash driver treats as FAIL regardless
  of MAE/PSNR values; prevents two-pure-black fake PASS).

Output: single-line JSON to stdout, error cases emit JSON too (no
multi-line stack traces polluting CI logs).  Exit codes: 0 = diff
completed (caller decides PASS/FAIL), 1 = input error, 2 = usage.

### Source anchor

- `tools/ae_parity_referee/diff_pixels.py` (NEW, ~110 LOC).
- `tests/visual/support/image_diff.cpp` (formula parity reference).
- `tests/visual/support/image_diff.hpp` (`ImageDiffResult` struct
  for cross-language metric naming consistency).

### Test lock (helper-level)

- `python3 -c "import py_compile; py_compile.compile('tools/ae_parity_referee/diff_pixels.py', doraise=True)"` exit 0.
- `python3 tools/ae_parity_referee/diff_pixels.py /nonexistent /nonexistent` exit 1, JSON `{"error":"ref_missing","path":"/nonexistent"}`.
- `python3 tools/ae_parity_referee/diff_pixels.py A B C` exit 2, JSON `{"error":"usage",...}`.
- Two identical PNGs (any size) → `mae_255: 0.0`, `psnr_db: "inf"`, `blank: false`.
- Two different-sized PNGs → JSON `{"error":"size_mismatch",...}` exit 1.
- Two pure-black 1x1 PNGs → `blank: true`.

### Failure mode (if silently broken)

- **Diverging from `image_diff.cpp` formula**: a future regression
  that changes MAE from sum-of-abs to max-of-abs would silently
  produce different verdicts from ctest goldens.  Formula
  documented verbatim in script docstring; future refactors must
  preserve the comment block.
- **Missing blank-frame guard**: two pure-black PNGs would
  yield MAE=0 + PSNR=inf, falsely PASSing.  The `blank` flag
  is computed unconditionally and the bash driver rejects
  `blank=True` regardless of MAE/PSNR.
- **numpy/scipy dependency creep**: would break on minimal
  dev envs.  Pillow + stdlib only — locked in import block.
- **Multi-line stack traces polluting CI logs**: all error
  paths emit JSON; no `print(traceback.format_exc())` calls.
  Locked by single-line `print(json.dumps(...))` pattern.

---

## Anti-greenwashing gate (the core decision)

### Spec

`AE_PARITY_MIN_PASS=10` (env-overridable).  The referee emits
exit 0 ONLY if at least 10 of the 15 cinematic scenes have ALL
frames PASS.  The "AE-like" claim is REJECTED otherwise (exit 1).

This is the **last** layer of the cat-2 rigor defense-in-depth:
1. **Per-scene visual-goldens** (TICKET-AE-PARITY-CINEMATIC-NN):
   each ae_*.cpp scene has its own PNG golden compared via
   ctest.  Catches single-scene regressions.
2. **Per-property killers** (TICKET-AE-PARITY-KILLER-WIGGLY-WAVE
   / EXPRESSION-SELECTOR / CHARACTER-OFFSET-VALUE-RANGE):
   the 3 property-level contracts (determinism, per-char formula,
   pre-shaping invalidation).  Catches property-level regressions.
3. **Per-scene referee** (THIS TICKET): engine vs AE-side mock
   comparison.  Catches "looks plausible to us but not AE-like"
   regressions.  Final gate before any "AE-like" claim is
   allowed.

The 10/15 threshold is the "two-thirds majority" rule: we don't
require ALL 15 to PASS (too brittle for early-stage parity),
but we don't accept fewer than 10 (would let through systematic
regressions).  CI can tighten to 15/15 via `AE_PARITY_MIN_PASS=15`
once the baseline is stable.

### Source anchor

- `tools/ae_parity_referee.sh` (the gate logic).
- `docs/tickets/TICKET-AE-LIKE-TEXT-ACTION-PLAN.md` Phase 3 spec.
- TICKET-AE-PARITY-SUITE (the 20 scenes; gate targets 15 in v1).
- TICKET-AE-PARITY-FLOOR (288+ PNG floor deliverable).

### Failure mode (if silently broken)

- **Gate too lenient (e.g. 5/15)**: systematic regressions would
  pass.  Default 10/15 is the "two-thirds" floor; CI must
  explicitly opt-in to lower values.
- **Gate too strict (15/15 in v1)**: early-stage false negatives
  would block legitimate progress.  10/15 is the v1 sweet spot;
  tightening to 15/15 is a forward-only move once the suite
  stabilizes.
- **No env-var override**: CI could not iterate the gate value
  per-PR.  `AE_PARITY_MIN_PASS` env var is the override channel.

---

## Per-commit lock

```bash
# Bash syntax check
bash -n tools/ae_parity_referee.sh                # exit 0

# Python syntax check
python3 -c "import py_compile; py_compile.compile('tools/ae_parity_referee/diff_pixels.py', doraise=True)"  # exit 0

# Help (sanity)
tools/ae_parity_referee.sh --help                 # exit 0, prints usage

# Scaffold (idempotent first-time setup)
tools/ae_parity_referee.sh scaffold               # exit 0
ls reference/                                      # README.md, after_effects/, chronon3d/

# Run with empty refs (anti-greenwashing: exit 1, no green PASS)
tools/ae_parity_referee.sh                         # exit 1, "no scene_NNN_frame_NN.png found"
tools/ae_parity_referee.sh --json                  # exit 1, JSON output
```

---

## Cross-cutting gates (must hold at the end of the commit)

```
[tools/check_doc_sync.sh]              exit 0  (gate #7 doc drift)
[tools/check_architecture_boundaries.sh] exit 0  (16/16 PASS)
[tools/wrap_push.sh origin main]       exit 0  (GATE-MNT-01 + per-branch rebase)
[bash -n tools/ae_parity_referee.sh]   exit 0  (bash syntax)
[py_compile compile]                   exit 0  (python syntax)
[ae_parity_referee.sh --help]          exit 0  (usage printed)
[ae_parity_referee.sh scaffold]        exit 0  (scaffold idempotent)
[ae_parity_referee.sh] (no refs)       exit 1  (anti-greenwashing gate)
```

---

## Forward-only contract

- The actual `reference/after_effects/scene_NNN_frame_NN.png`
  AE-side mock PNGs are NOT committed in this ticket (they are
  the deliverables of TICKET-AE-PARITY-SUITE).  The referee is
  functional TODAY; the gate is BLOCKED on TICKET-AE-PARITY-SUITE
  populating the AE mocks + TICKET-AE-PARITY-FLOOR populating
  the engine output.  When both deliver, `tools/ae_parity_referee.sh`
  is the gate.  Until then, the script exists + is correct + is
  ready to gate.
- ImageMagick migration is a future option (the `compare` tool
  is more efficient on large images), but not required for v1.
  Python+Pillow is sufficient for the 5/255 + 30dB thresholds.
- Multi-resolution support (engine output at different resolution
  than AE mock) is forward-only — current script requires
  `ref.size == eng.size`.  A future `AE_PARITY_AUTO_RESIZE=1`
  flag could enable nearest-neighbor resize on the engine output
  to match the AE mock.
- The 10/15 gate is the v1 floor.  Forward-only tightening to
  12/15, 15/15, etc. is expected as the suite stabilizes.

---

## Cross-link

- `docs/tickets/TICKET-AE-LIKE-TEXT-ACTION-PLAN.md` Phase 3 (referee spec)
- TICKET-AE-PARITY-SUITE (the 20 cinematic scenes that will populate refs)
- TICKET-AE-PARITY-FLOOR (288+ PNG floor deliverable)
- TICKET-AE-PARITY-KILLER-WIGGLY-WAVE (sibling per-property killer)
- TICKET-AE-PARITY-KILLER-EXPRESSION-SELECTOR (sibling per-property killer)
- TICKET-AE-PARITY-KILLER-CHARACTER-OFFSET-VALUE-RANGE (sibling per-property killer)
- [docs/adr/ADR-014-text-golden-coverage.md](../adr/ADR-014-text-golden-coverage.md) (the AGENTS Cat-5 freeze-compliant doc-only precedent for `tools/test_golden_driver.sh` sibling)
- `docs/FOLLOWUP_TICKETS.md` (canonical index — DONE row with `(this commit)` SHA placeholder)
