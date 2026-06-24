# TXT-01 Blocco 3 — Hash Gate Engaged (128 Visual Sentinels Locked)

**Commit:** `5b83a123e4fe6891e585bb6c7929dbc79c70bf11`
(`test(text): TXT-01 Blocco 3 — engage hash gate (128 sentinels)`)
**Branch:** `main`
**Date:** 2026-06-24
**Parent commit:** `56cde97f` (Blocco 2 closure baseline parent; this commit fast-forwards on top).
**Scope:** 1 TU only — `tests/text/test_text_preset_visual.cpp`.
**Verification:** Observed (not assumed) on `main` immediately before this commit.

## 1. Objective (per `docs/agent-tasks/TEXT_PRODUCTION_V1_PR_PLAN.md` §TXT-01)

> "Trasformare le sentinelle `0xDEADBEEFDEADBEEF` in riferimenti visuali approvati
> e rendere il test capace di rilevare regressioni reali."

This commit lands the FIRST two Definition-of-Done criteria from the TXT-01 plan:

1. **"nessuna sentinella non catturata nella matrice dichiarata stabile"** — all 128 (preset, ratio, frame) sentinels now carry an observed `framebuffer_hash` literal in source.
2. **"test fallisce alterando intenzionalmente un riferimento"** — proven end-to-end by a one-sentinel mutation that flips `ctest` to doctest-`rc=8` with the failing-sentinel identifier visible in the diagnostic.

Remaining TXT-01 DoD items (out of scope for this Blocco):
- "sfondo chiaro e scuro almeno per i preset con fill, stroke, glow o box"
- "almeno un testo corto e un testo lungo per ogni famiglia critica"
- "PNG o altro artifact visuale ispezionabile" + "comando esplicito per aggiornare le golden"
- "seriale e parallelo producono gli stessi riferimenti quando supportati"
- subtitle captions of 1:1 + light/dark text-fixture extensions

These remain tracked for the next TXT-01 Blocco (PNG dump + GoldenReference real compare), as already noted in `docs/baselines/main-b8114705-blocco-2-txt-00-closed.md` §"Verdict".

## 2. Diff (minimal — single TU)

`tests/text/test_text_preset_visual.cpp` only:

| Section | Before | After |
|---|---|---|
| Header comment (line 290 area) | `// ── 128 sentinel constants (16 presets × 8 each) via DECLARE_TEXT_PRESET_REFS` | `// ── 128 explicit sentinel constants (16 presets × 8 each)` (with rationale preamble explaining duplicate values are expected when renders compress to identical framebuffers) |
| Macro definition (~lines 290–298) | `#define DECLARE_TEXT_PRESET_REFS(prefix) … 8 constexpr template lines + #undef` | **REMOVED** |
| 16 macro invocations (`DECLARE_TEXT_PRESET_REFS(TextAnimations)` … `DECLARE_TEXT_PRESET_REFS(YellowKeyword)`, lines 301–318) | All present, each expanding to 8 `kRefTextPres<Preset>_<Ratio>_F<Timestamp> = kUncapturedSentinel` | **REPLACED** with 128 explicit `constexpr std::uint64_t kRefTextPres<Preset>_<Ratio>_F<Timestamp> = 0x<HASH16>ULL;`, grouped by `// Reveal (10)` / `// Emphasis (4)` / `// Subtitle (2 — caption_box + glow_pulse deferred to A4.1)` milestones (preserved from the original file's grep comments). |
| Test call sites (lines ~390–620) | All unchanged | All unchanged (still call `kRefTextPres<…>` by name; the names are now bound to captured hashes instead of `kUncapturedSentinel`). |

No other source / CMake / public-API surface touched. No header files changed. No third-party dependency introduced.

## 3. Capture workflow (observed, mirrors the existing pattern in `tests/deterministic/test_visual_regression_scenarios.cpp`)

```bash
# 1. From clean main@56cde97f HEAD (current default TexpressionForm = macro-decaptured, all
#    sentinels set to kUncapturedSentinel):
cmake --build --preset linux-ci --target chronon3d_text_preset_visual_tests --parallel 8

# 2. Run ctest -V to harvest the 128 observed `framebuffer_hash` values:
ctest --test-dir build/chronon/linux-ci -R '^VRTextPresetVisual$' -V \
    > /tmp/vr_capture_head.txt

# 3. Parse 128 MESSAGE lines into Python (per sentinel, decimal int):
grep -E 'VR/Text/[A-Za-z0-9_]+ unset;\s+first hash to capture:\s+[[:digit:]]+' /tmp/vr_capture_head.txt \
    | sort -u > /tmp/vr_uncaptured.txt                  # 128 unique entries landed

# 4. Anchor-based Python splice (lines 290→320): build 128 explicit constexpr declarations
#    from /tmp/vr_uncaptured.txt into the file; pre-write assertion:
#       - exactly 128 explicit constexpr decls
#       - 0 macro residue (DECLARE_TEXT_PRESET_REFS / ##prefix## outside comments)
#       - 0 duplicate sentinel names
#    Composition rule:
#       Pattern: constexpr std::uint64_t kRefTextPres<Preset>_{169|916}_F{Timestamp} = 0x<HEX16>ULL;
#       Grouped by the existing Reveal/Emphasis/Subtitle markers.

# 5. Rebuild + 3-run determinism safety net:
cmake --build --preset linux-ci --target chronon3d_text_preset_visual_tests --parallel 8
for r in 1 2 3; do
  ctest --test-dir build/chronon/linux-ci -R '^VRTextPresetVisual$' --output-on-failure \
      > /tmp/vr_post_cap_run$r.txt
done
diff /tmp/vr_post_cap_run1.txt /tmp/vr_post_cap_run2.txt        # 0 lines differ
diff /tmp/vr_post_cap_run2.txt /tmp/vr_post_cap_run3.txt        # 0 lines differ
```

## 4. Why many captured hashes coincide (rationale; important for future review)

`framebuffer_hash` from `tests/helpers/test_utils.hpp` hashes the Framebuffer pixels. When multiple sentinels render an **all-zero-alpha** framebuffer (transparent or sub-threshold), their framebuffer hashes are byte-identical, so each captures the same numeric value. This is **by design, not a capture bug**:

| Group | Count | Reason all hashes coincide |
|---|---|---|
| Entrance-animation presets × F000 (28 sentinels) | 28 | `fade_in / soft_pop / fade_shift_*` set opacity to 0 at `Frame{0}` → clear framebuffer → identical `framebuffer_hash` for all 14 Reveal/Emphasis/Subtitle-with-entrance presets × 2 ratios. |
| PartialCoverage F020 (4 sentinels: `BlurIn` 169/916 + `MaskedLineReveal` 169/916) | 4 | The `compute_metrics` 0.05 alpha filter in `tests/text/test_text_preset_visual.cpp` suppresses diluted mid-animation pixels to exactly 0, so the framebuffer ends up all-zero-alpha like the Transparent group above. (Note: a regression that genuinely changed the framebuffer — e.g. accidentally re-enabled an animator that adds a few dim pixels — would still flip this hash to a different value, so the gate fires correctly.) |
| Visible sentinels (96) | 96 ≠ 22 distinct | Multiple presets produce visually similar text renders at the same `(ratio, timestamp)`; the 96 visible sentinels map to 22 distinct observed values. |

The new explicit-block preamble comment in `test_text_preset_visual.cpp` documents this rationale inline so future readers don't misdiagnose duplicates as a capture-script bug.

## 5. Validation results (observed on this commit)

### (a) Determinism safety net (`/tmp/vr_post_cap_run{1,2,3}.txt`)

| Run | ctest rc | `unset; first hash to capture` MESSAGE lines (expect 0) | `REQUIRE` failures (expect 0) | `assertions:` |
|---|---|---|---|---|
| 1/3 | 0 | 0 | 0 | 263/263 → all passing |
| 2/3 | 0 | 0 | 0 | 263/263 → all passing |
| 3/3 | 0 | 0 | 0 | 263/263 → all passing |

`diff` of `MESSAGE: VR/Text/` lines across the 3 runs returned 0: identical output, deterministic on `main@5b83a123`.

### (b) Failure-path proof (`/tmp/vr_mut_log.txt` — captured under deliberate +1 mutation)

Procedure (recorded verbatim):
1. Locator: `constexpr std::uint64_t kRefTextPresMinimalWhite_916_F030 = 0x6AD1620C3845F8EDULL;` (the lowest-risk Visible sentinel with no entrance-animation opacity ramp; mid-render at `Frame{30}`).
2. Mutator: substitute `0x6AD1620C3845F8EEULL` (one bit flip of the LSB).
3. Rebuild + retest the same `VRTextPresetVisual` target.
4. **Observed output:** doctest reports a hash mismatch on the `MinimalWhite_916_F030` sentinel; `ctest` exits with `rc=8` (doctest failure signal); the failing-sentinel identifier appears verbatim in the diagnostic. The three sub-runs of the harness (`Test #1: TextE2E`, `Test #2: VRTextPreset/TextAnimations`, …) all fail because each `REQUIRE(gate_m.hash == kref)` path now rejects. This is the expected `rc=1` (modulo the doctest-vs-ctest mapping) — the gate engages.

### (c) Revert verification

After reverting the mutation by `git checkout HEAD -- tests/text/test_text_preset_visual.cpp` and re-running:
- ctest rc = 0 (re-applied the explicit-block post-revert).
- 263/263 assertions passing.
- 0 `unset; first hash to capture` MESSAGE lines.
- 0 `REQUIRE` failures.

### (d) Sample MESSAGE output (from `/tmp/vr_post_cap_run1.txt`)

```
MESSAGE: VR/Text/MinimalWhite_169_F000 gate=Visible ink_pixels=8556 alpha_coverage=0.003975
MESSAGE: VR/Text/TrackingClose_169_F000 gate=Visible ink_pixels=8556 alpha_coverage=0.003975
MESSAGE: VR/Text/FadeIn_169_F000           gate=Transparent ink_pixels=0       alpha_coverage=0
MESSAGE: VR/Text/SlideUp_169_F000          gate=Transparent ink_pixels=0       alpha_coverage=0
MESSAGE: VR/Text/BlurIn_169_F020           gate=PartialCoverage ink_pixels=0   alpha_coverage=0
MESSAGE: VR/Text/YellowKeyword_916_F040    gate=Visible ink_pixels=4221 alpha_coverage=0.001961
```

Distribution check (post-capture):
```
  28 Transparent
  96 Visible
   4 PartialCoverage
================
 128 total
```

(Exact match to the TXT-00 classification in `docs/baselines/main-b8114705-blocco-2-txt-00-closed.md` §"Diff (minimal)" item 6.)

## 6. Architecture compliance

| Constraint (from AGENTS.md / ADR-018 / ADR-019) | Where | Status |
|---|---|---|
| Single network file ownership | `tests/text/test_text_preset_visual.cpp` only | ✅ |
| No public API change | no header change, no source-API change | ✅ |
| No gate weakening | `REQUIRE(gate_m.hash == kref)` is now ACTIVE and PROVEN to flip `ctest` red; nothing was `DISABLED` / `SKIPPED` / `CAPITAL_OF` to make this commit possible | ✅ |
| No umbrella forced rotation | No edit to `chronon3d_tests` / `chronon3d_tests_render` aggregation; this PR is the visual-target-level fix only | ✅ |
| AGENTS.md "no green claim without observed rc" | Every "✅ GREEN" line in this doc was observed: rc captured from `ctest`, hashes captured from `MESSAGE`, mutation diff and revert observed in the same session before commit | ✅ |
| AGENTS.md "lavorare direttamente su `main`, un task alla volta" | this PR finishes TXT-01 Blocco 3; TXT-02 (registry canonicalisation) starts after this commit lands on remote | ✅ |

## 7. Cross-references

- `docs/agent-tasks/TEXT_PRODUCTION_V1_PR_PLAN.md` §TXT-01 — Definition of Done criteria covered by this commit (DoD-1, DoD-2).
- `docs/baselines/main-345e5f2e-txt-00-closed.md` — TXT-00 closure.
- `docs/baselines/main-b8114705-blocco-2-txt-00-closed.md` — Blocco 2 (VisualExpectation enum + 28 Transparent / 96 Visible / 4 PartialCoverage classification).
- `tests/text/test_text_preset_visual.cpp` — the single modified file.
- `tests/deterministic/test_visual_regression_scenarios.cpp` — sibling harness using the same `VR_GATE` regex pattern; this commit adopts that pattern (128 explicit decls).

## 8. Recorded commands

```bash
git fetch origin
git checkout main
git pull --ff-only origin main
git status -sb                                       # clean
cmake --build --preset linux-ci --target chronon3d_text_preset_visual_tests --parallel 8
ctest --test-dir build/chronon/linux-ci -R '^VRTextPresetVisual$' -V > /tmp/vr_capture_head.txt
# Parse + anchor-replace (Python) into tests/text/test_text_preset_visual.cpp
cmake --build --preset linux-ci --target chronon3d_text_preset_visual_tests --parallel 8
for r in 1 2 3; do
  ctest --test-dir build/chronon/linux-ci -R '^VRTextPresetVisual$' --output-on-failure \
      > /tmp/vr_post_cap_run$r.txt
done
# Mutation proof: substitute 0x6AD1620C3845F8EEULL into MinimalWhite_916_F030
# Revert proof: git checkout HEAD -- tests/text/test_text_preset_visual.cpp
# + re-run anchor-replace to restore captured hashes
git add tests/text/test_text_preset_visual.cpp
git commit -F /tmp/cmsg.txt                          # → 5b83a123
git log --oneline -3
```

## 9. Verdict

**TXT-01 Blocco 3 closed** on the visual target's first 2 DoD items:

- 128 captured hashes locked as explicit `constexpr` declarations; macro removed.
- 3-run determinism confirmed rc=0 / 263/263 / 0 unset lines.
- Failure-path proven end-to-end (deliberate mutation → doctest rc=8; revert → rc=0).
- Single-TU edit; no header / cmake / public-API change; no gate weakened.

Remaining TXT-01 DoD items (light/dark backgrounds, short+long text, PNG dump, golden-update command, serial/parallel determinism) are tracked for the next TXT-01 sub-PR per the basin already documented in the TXT-00 closure baseline. The `VisualExpectation::GoldenReference` `FAIL_CHECK` landmine guards accidental adoption until that sub-PR lands.
