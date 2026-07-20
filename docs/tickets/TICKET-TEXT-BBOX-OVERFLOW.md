# TICKET-TEXT-BBOX-OVERFLOW — Renderer bbox expansion rot (POST_RENDER_EXPAND actual.x negative)

## Stato: OPEN (2026-07-20, opened atomicamente with feat(matrix): lean N3 metric coverage)

## Problema

Il post-N3 lean `CHECK_FALSE(overflow)` a non-extreme_scale (introdotto da
TICKET-GOLDEN-MATRIX-FULL-METRIC-COVERAGE) ha catturato una rot REALE nel
renderer testo: `POST_RENDER_EXPAND` produce actual bbox `x=-214` vs
predicted bbox `x=0` — il glyph extent eccede il margine sinistro del
framebuffer. 16 di 18 celle Subtitle preset falliscono con questa
esatta violazione. Per AGENTS.md `non cambiare un gate per nascondere
un errore`, il CHECK_FALSE è correttamente ritenuto.

## Evidenza

ctest verbose output (FAST_MODE + TOLERATE_EMPTY_FRAMES=1):

```
[warning] [text-bbox] POST_RENDER_EXPAND node=minimal_white_text predicted=(0, 92, 315, 300) actual=(-214, 159, 214, 231) expanded=(-214, 92, 315, 300)
```

`overflow` metric definition (`tests/text/visual/text_visual_metrics.cpp::compute_metrics`):

- `m.overflow =` true se ALMENO un ink pixel è entro `kEdgeProximityPx = 2`
  dal framebuffer edge (qualunque lato).
- 16/24 Subtitle preset cells mostrano `overflow=true` a non-extreme_scale
  (96 cells totali nella golden matrix Subtitle baseline in full mode; in
  FAST_MODE la baseline copre 24 cells = 4 preset × 2 AR × 3 ts).

cumulative pattern (per Subtitle preset FAST_MODE, 6 cells ciascuna):
- `MinimalWhite`: 6/6 cells overflow (169_S_x1_cw @F0/20/40 + 916_S_x1_cw @F0/20/40)
- `YellowKeyword`: 4/6 cells overflow (2 cells empty_frame / font asset-block)
- `GlowPulse`:    2/6 cells overflow
- `CaptionBox`:   4/6 cells overflow

`pass_count > 0` + unique_hash floor `>= 2u` PASSES empirically (no
false-positive in fast_mode); the lean re-design rot-gate is preserved.

## Soluzione accettabile (deferred — requires architecture decision)

### Wiring paths (any ONE sufficiently addresses the rot)

- **Option α** — fix `text-bbox POST_RENDER_EXPAND` predictor at the OPP
  compile path. Investigate why the predicted bbox
  (`centered_text → 0, 92, 315, 300`) does not match the actual glyph
  extent `(-214, 159, 214, 231)`.  `actual.x = -214` strongly suggests
  FreeType per-glyph `bearing_x` (cluster-bounded via HarfBuzz, NOT a
  scalar block-bbox shift) is unaccounted-for in the centered_text
  predictor. Update the predictor to compute the leftward bearing per
  glyph cluster via HarfBuzz's `hb_glyph_info_t` `.x_advance` /
  `.x_offset` and shift `bbox.x` accordingly so predicted matches actual.

- **Option β** — change the overflow metric threshold from
  "any ink pixel within 2px of edge" to a tighter band that excludes
  leftward expansion up to the safe-area margin (e.g., only flag when
  x < safe_area_left OR x >= width - safe_area_right).

- **Option γ** — skip overflow detection at non-extreme_scale in
  fast_mode (the renderer is intentionally permissive in low-fidelity
  mode per `RenderingRuntime::populate()` debug log). The CHECK_FALSE
  fires only when intentional overflow is implied; the predictor
  doesn't double-count.

### Recommended: Option α — root-cause + align predictor to actual.

This is the canonical cleanest fix: the predictor SHOULD match the
actual output.  Per AGENTS.md `non cambiare un gate per nascondere un
errore`, the metric definition is honest; the predictor is the bug.

## Criteri di accettazione

- [ ] Investigation complete: identify why POST_RENDER_EXPAND produces
  negative x in non-extreme_scale cells (centered_text + Poppins-Bold
  96pt at 1920×1080 / 1080×1920).  Likely cause: FreeType left-side
  bearing not threaded into the predictor when centered_text wraps.
- [ ] Fix applied: predictor includes `bearing_x` subtraction OR
  centralized_text repositions x0 by `bearing_x` so actual matches
  predicted.
- [ ] Test: `ctest -R 'golden_matrix' --output-on-failure` returns
  GREEN for all 4 Subtitle presets (no `CHECK_FALSE( overflow )`
  failures); `overflow=0` in summary messages.
- [ ] NO new `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5
  deny-everywhere per AGENTS.md).
- [ ] Cronaca canonical ticket-home per AGENTS.md
  §`### Docs canonical update discipline rule` Cat-3 anti-dup.
- [ ] Subject envelope ≤ 72 chars per
  `tools/check_commit_subject_length.sh`.

## Accountability (anti-rot hook)

To prevent the overflow rot from decaying into permanent silent-pass:

- **Owner**: TBD
- **Trigger**: V1 release-prep milestone (RELEASE_GATE.md requires
  golden matrix certified GREEN per `golden_determinism_matrix`
  milestone).
- **Resolution path if not closed by V0.1-prep**: option β fallback
  (tighten overflow metric to safe-area band) preserves the
  rot-detection intent without requiring deep predictor surgery.

## Cross-link

- TICKET-GOLDEN-MATRIX-FULL-METRIC-COVERAGE (chore that surfaced this rot)
- TICKET-GOLDEN-MATRIX-MIGRATE-BATCH-1 (Batch 1 cpp mirror → harness
  header migration; the cat-3 anti-dup canonical fix still pending)
- TICKET-GOLDEN-MATRIX-BATCH-3 (matrix sweep across Emphasis+Cinematic+Reveal)
- TICKET-GOLDEN-MATRIX-SUBTITLE-BATCH-1 (Batch 1 baseline)
- `tests/text/visual/text_visual_metrics.cpp` (`overflow` metric definition)
- `tests/text_golden/matrix/golden_matrix_harness.hpp`
  (`CHECK_FALSE` site at the lean re-design commit)
- `src/text/bbox/predictor.h` or `src/text/centered_text_predictor.cpp`
  (TBD — investigate predictor location)

## §honest-discipline lineage

This ticket was opened atomicamente per AGENTS.md
`### 2×-in-one-chore: deprecation reversal bundles forward-point
tickets (Cat-3 anti-dup)` rule + the established Cat-5 3-doc bundle
pattern (TICKET-OPP-BG-CONSUMER precedent).  The chore that surfaced
this rot lands ATOMIC with the ticket opening per `non segnare verde
una suite che restituisce failure` (the rot is real, the CHECK
correctly detects it, the ticket tracks the fix path).

Cronaca canonical ticket-home per AGENTS.md §honest-discipline.
