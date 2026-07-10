# Glow A/B test — `BUG 2` (TICKET-TEXT-GLOW-DARKENING) — Step 3 — 2026-07-10

**Status: 🛑 BLOCKED** — experiment PARTIAL.  WITH case rendered + measured.
WITHOUT case NOT rendered (blocked by pre-existing build rot in
`include/chronon3d/scene/builders/builder_params.hpp`).  No PASS/FAIL
verdict can be issued for the "Glow darkens" claim.

## Hypothesis

> The `glow_intensity=0.5` parameter on `AnimTypewriterGlow` causes
> **darkening** of the revealed characters (vs. `glow_intensity=0.0`).

If true → **FAIL** (claim confirmed, bug repro).
If false → **PASS** (claim excluded, no darkening at this intensity).

## Methodology

| Parameter            | Value                                                                                            |
|----------------------|--------------------------------------------------------------------------------------------------|
| Composition (WITH)   | `AnimTypewriterGlow` (production, `glow_intensity=0.5`)                                          |
| Composition (WITHOUT)| `AnimTypewriterGlowNoGlow` (sibling, `glow_intensity=0.0`, NEW — see `tests/visual/glow_ab/`)     |
| Frame                | 140 of 160 (post-reveal, stable state, both lines fully revealed)                                |
| Render command       | `chronon3d_cli render <comp> --frame 140 -o <path>.png` (existing binary `build/apps/chronon3d_cli/chronon3d_cli`) |
| Output size          | 1920×1080 RGBA PNG                                                                                |
| Bbox (text ink)      | `(460, 280, 1460, 800)` — 1000×520 region covering the 2-line text band of the typewriter         |
| Luminance formula    | Rec.709: `0.2126·R + 0.7152·G + 0.0722·B` (per-pixel, mean over bbox)                              |
| Threshold            | `|delta_pct| < 2.0%` → PASS (darkening excluded); `delta_pct ≤ -2.0%` → FAIL (darkening confirmed) |
| Measurement tool     | `tools/measure_glow_darkening.py` (stdlib + Pillow only, no numpy)                                |

## Results (PARTIAL)

### WITH case — `AnimTypewriterGlow` (glow_intensity=0.5) — MEASURED ✅

Rendered at 2026-07-10 14:58:06 UTC, exit 0, 83.6 KB PNG.

| Metric                                     | Value                       |
|--------------------------------------------|-----------------------------|
| PNG file                                   | `tests/visual/glow_with/AnimTypewriterGlow.png` |
| Pixel size                                 | 1920×1080 RGBA              |
| Bbox size                                  | 1000×520 = 520 000 pixels   |
| Mean Rec.709 luminance (bbox)              | **2.6379** / 255            |
| Mean Rec.709 luminance (full frame, ref)   | 2.6560 / 255                |
| Bbox extrema R / G / B                     | 0–65 / 0–69 / 0–75          |
| Bbox extrema full-frame                    | 0–66 / 0–70 / 0–76          |

Interpretation: the WITH-case frame is *very dark* in the text region
(mean lum ~2.6/255 ≈ 1%). The text (white ~ 255, 255, 255) occupies
a small minority of the bbox; the dark "minimalist grid" background
dominates the mean. The extrema in the bbox reach ~75, which
corresponds to the white text glyphs (with glow halo bleeding into
~75 RGB on the dark background).

### WITHOUT case — `AnimTypewriterGlowNoGlow` (glow_intensity=0.0) — NOT RENDERED 🛑

The sibling composition `AnimTypewriterGlowNoGlow` was added
(registration in `apps/chronon3d_cli/cli_init.hpp` is staged, source
files at `tests/visual/glow_ab/glow_ab_compositions.{hpp,cpp}`) but
**the rebuilt `chronon3d_cli` binary failed to compile** due to a
**pre-existing build rot** (NOT introduced by this commit):

```
include/chronon3d/scene/builders/builder_params.hpp:188:1:
    error: expected unqualified-id before 'using'
  188 | using TextParams = TextSpec;
include/chronon3d/scene/builders/builder_params.hpp:237:1:
    error: expected unqualified-id before 'using'
  237 | using TextRunParams = TextRunSpec;
```

This rot was introduced by commit `2ec4a49c feat(text): deprecate
TextParams/TextRunParams, migrate 48 files to TextRunSpec` which
added deprecation `using` aliases in `builder_params.hpp` that are
not accepted by the current compiler flags (likely a missing
`[[deprecated]]` attribute or namespace scope issue).

The **Jul 8 pre-existing binary** (`build/apps/chronon3d_cli/chronon3d_cli`,
timestamp `Jul 8 18:12`) works correctly for `AnimTypewriterGlow`
(WITH case rendered successfully) but does **not contain** the new
sibling composition.  Rebuilding is required, and the rebuild fails.

## Conclusion

**🛑 BLOCKED — no PASS/FAIL verdict issued.**

The "Glow darkens revealed characters" claim is **NEITHER confirmed
NOR excluded** by this experiment:
- WITH case measured: mean_lum_bbox = 2.6379 (dark frame as expected).
- WITHOUT case: cannot render without rebuilding the CLI, which is
  blocked by the pre-existing build rot.

Per the user's constraint (`NON toccare il codice di produzione —
solo experiment`), the build rot is out of scope for this commit.

## Resumption steps (for the next agent)

Once `builder_params.hpp` build rot is fixed (out of scope here):

```bash
# 1. Rebuild chronon3d_cli with the new sibling composition
cmake --build build --target chronon3d_cli -j$(nproc)

# 2. Render the WITHOUT case
mkdir -p tests/visual/glow_without
build/apps/chronon3d_cli/chronon3d_cli render AnimTypewriterGlowNoGlow \
    --frame 140 \
    -o tests/visual/glow_without/AnimTypewriterGlowNoGlow.png

# 3. Run the A/B comparator
python3 tools/measure_glow_darkening.py \
    tests/visual/glow_with/AnimTypewriterGlow.png \
    tests/visual/glow_without/AnimTypewriterGlowNoGlow.png \
    --bbox 460,280,1460,800 \
    --threshold-pct 2.0
# Expected: PASS (no darkening) — otherwise FAIL (claim confirmed)

# 4. Update this report with the verdict and commit
```

The new composition's registration in `apps/chronon3d_cli/cli_init.hpp`
is already staged (1 include + 1 `registry.add()` line, both
additive, both commented with the A/B test intent).  Once the build
rot is fixed, `chronon3d_cli list` will show `AnimTypewriterGlowNoGlow`
alongside `AnimTypewriterGlow`, and the resumption steps above will
complete the experiment end-to-end.

## Files in this commit

| File                                                | Status | Purpose                                                  |
|-----------------------------------------------------|--------|----------------------------------------------------------|
| `tests/visual/glow_ab/glow_ab_compositions.hpp`     | NEW    | Sibling composition factory declarations                  |
| `tests/visual/glow_ab/glow_ab_compositions.cpp`     | NEW    | Sibling composition definition + register function        |
| `tools/measure_glow_darkening.py`                   | NEW    | A/B comparator (stdlib + Pillow only)                    |
| `docs/baselines/2026-07-10-glow-ab-result.md`       | NEW    | This report                                              |
| `apps/chronon3d_cli/cli_init.hpp`                   | +2 lines (additive) | Register the new sibling composition (dormant until build rot is fixed) |
| `.gitignore`                                        | +3 lines (additive) | Exclude `tests/visual/glow_with/` and `tests/visual/glow_without/` (transient experiment PNGs) |

## Commit

This report is the body of the atomic commit
`test(visual): BUG 2 Glow A/B test (Step 3) — partial, build rot blocks WITHOUT render`.
The commit lands on `main`; the experiment infrastructure is staged
and the WITHOUT case will activate once the build rot is resolved.

## References

- TICKET-TEXT-GLOW-DARKENING (FOLLOWUP_TICKETS.md, OPEN)
- `tools/measure_glow_darkening.py` — A/B comparator
- `tests/visual/glow_ab/glow_ab_compositions.{hpp,cpp}` — sibling composition
- `content/examples/text/text_animations.cpp` — production `anim_typewriter_glow` (UNTOUCHED)
- `content/common/text_reveal_helpers.hpp` — `build_2line_typewriter` + glow branch (UNTOUCHED)
- Commit `2ec4a49c` — pre-existing build rot source (out of scope for this fix)
