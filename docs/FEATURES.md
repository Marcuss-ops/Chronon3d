# Chronon3D — Feature Reference

Current repository maturity is tracked in [`STATUS.md`](STATUS.md).

## Stable feature areas

- SVG path import V1: `M/L/H/V/C/Q/Z`, including relative forms.
- Text: FreeType/HarfBuzz shaping, FriBidi segmentation, layout, auto-fit,
  overflow control, presets, per-glyph animation, gradients, strokes, and glyph cache.
- Images, layers, masks, blend modes, 2.5D camera, and software effects.
- Video output and telemetry are controlled by build options.

Known limits include incomplete ICU-style CJK line breaking, no color-emoji
table support, and SVG import limited to the first path without full SVG styling.

## Expressions V2 — experimental quarantine

Expressions V2 is present on `main`, but it is **not** a stable public feature.

| Surface | Actual state |
|---|---|
| Root | `experimental/expressions/` |
| Headers | `experimental/expressions/include/chronon3d_experimental/expressions/v2/` |
| Sources/tests | Under `experimental/expressions/` |
| Build switch | `CHRONON3D_BUILD_EXPERIMENTAL=ON` |
| Default | Excluded |
| Installed/exported SDK | No |
| Productive render-path integration | No |

The old `CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2` option is only a
deprecated no-op compatibility key.

Corrections:

- `TICKET-003` is closed; the `chrono3d/...` include typo was fixed.
- `TICKET-004` is closed; the CMake include visibility/path issue was fixed.
- They are historical records, not current promotion blockers.
- `TICKET-005` still tracks separate follow-up work such as the missing public
  `keyframes()` test surface.
- `TICKET-EXP2-G3` tracks real `AnimatedValue` integration.

Promotion requires **all eight** gates in
[`EXPRESSIONS_V2_PROMOTION.md`](EXPRESSIONS_V2_PROMOTION.md), including:

1. real productive integration with one parser/VM;
2. benchmark evidence;
3. a V1→V2 replacement/deletion map;
4. a concrete retirement deadline;
5. permanent single-parser/VM/dependency-graph enforcement.

Do not include `chronon3d_experimental/...` from stable production code before
the approved quarantine-removal change.
