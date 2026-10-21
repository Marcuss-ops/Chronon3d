# Ticket — Remaining SVG text support

**Status:** In progress
**Parent:** [TICKET-SVG-RENDER-PLAN.md](TICKET-SVG-RENDER-PLAN.md)
**Handoff baseline:** remote `main` at `a204d0d6` (2026-10-21); recheck the
current `main` head before implementation because the other workstation may
have advanced it.

## Goal and constraints

Close the remaining useful SVG text gaps using Chronon's existing C++ importer,
HarfBuzz shaping, font resolver, RenderPlan, and software renderer. This ticket
does not promise complete browser parity. Keep runtime implementation in C++;
use WPT and permissively licensed projects as algorithm/test references, not as
a Rust rendering dependency. Preserve fail-closed behavior for forms outside
the implemented subset.

## Already supported on the handoff baseline

- Mixed-content `<text>`/`<tspan>` order, inherited paint, font changes,
  whitespace preservation, chunk positioning and script-aware baseline
  placement.
- Same-font styled text shaping across compatible LTR, mixed-script and Arabic
  RTL chunks; paint-only boundaries inside a shaped grapheme preserve the base
  geometry and alpha.
- `dominant-baseline`, bounded `baseline-shift`, text fill/stroke and stroke
  dash/cap/join, plus linear/radial gradient fill and stroke.
- Bounded pattern text paint: explicit integer-pixel `userSpaceOnUse` tiles
  with solid path/primitive children; raw gates cover fill/stroke and repeats.
- Ordinary text `x`, `y`, `dx`, and `dy` coordinate lists, including paired
  coordinates under rotated/skewed transforms.
- Static local `textPath` references with `startOffset`, `text-anchor`,
  `x`/`y`/`dx`/`dy` coordinate lists, and projection into transformed path
  coordinates. Existing gates verify software pixels and repeat determinism.

## Open work

### P0 — Text advance and character-position attributes

- Implement `textLength` and `lengthAdjust="spacing|spacingAndGlyphs"` on
  `<text>` and nested `<tspan>` scopes. Apply nested constraints inner-to-outer
  while preserving shaped advances and per-glyph transforms.
- Add inherited `letter-spacing`, `word-spacing`, and per-character `rotate`
  with cluster-aware behavior. Reject or explicitly define values that split a
  ligature/grapheme; do not silently change shaping.
- Define behavior when these attributes combine with style runs, whitespace,
  coordinate lists, text anchoring, and each other.
- **Accept when:** loader and C ABI tests verify decoded constraints and glyph
  placement; software raw gates separately measure nested child and parent
  widths against targets, compare the two `lengthAdjust` modes, prove spacing
  and rotation changes, check unsupported cluster splits fail closed, inspect
  preview PNGs, and confirm byte-identical repeat frames.

### P1 — Complete the bounded pattern-paint contract

- Extend pattern children to cover groups and supported gradient/stroke paint
  inside a tile.
- Add `objectBoundingBox` tile units and `href` inheritance with cycle and
  unsupported-reference checks.
- Preserve transforms, alpha composition, fill rules, and fill/stroke pattern
  phase through the existing paint/cache/fingerprint paths.
- **Accept when:** loader/C ABI tests cover each supported pattern form and
  each rejection; raw fill/stroke gates inspect tile phase, bounds, alpha, and
  repeated-frame equality.

### P1 — TextPath shaping and remaining SVG attributes

- Current lowering emits independent shaped-cluster layers on a path, so
  joining-script shaping across clusters is not guaranteed. Preserve shaping
  across compatible text chunks before path placement, then map glyph clusters
  to path distances without reversing visual order for RTL.
- Decide and implement which non-default `method`, `spacing`, and `side`
  values belong to the supported subset; validate unsupported values explicitly.
- Keep animated and external path references fail-closed unless a real Chronon
  asset requires them.
- **Accept when:** C++ loader/C ABI fixtures cover LTR, RTL and joining scripts,
  style boundaries, anchors, supported attribute values and negative cases;
  raw gates compare glyph order, baseline/path progression and repeat frames.

### P2 — Vertical writing and font-metric edge cases

- Add SVG vertical writing modes and their baseline/orientation rules using
  existing shaping and raster APIs, or document this feature as out of scope for
  the importer contract.
- Replace `sub`/`super` baseline-shift approximations with font/spec-derived
  metrics where available; add a font fixture that actually contains matching
  OpenType BASE records to verify the positive lookup path.
- **Accept when:** loader/C ABI coverage exercises horizontal and vertical
  Latin/CJK samples, baseline identifiers, font fallback and BASE-table
  records; software renders are visually inspected and deterministic.

### P2 — SVG text inside isolated groups

- Connect text runs to the existing nested SVG group opacity/transform
  composition path, or retain an explicit unsupported error if compositing
  text surfaces would violate the canonical renderer contract.
- **Accept when:** overlap/opacity and nested-transform raw-pixel oracles prove
  text and vector children composite consistently; repeat frames match.
- Coordinate with [TICKET-SVG-GROUP-ISOLATION.md](TICKET-SVG-GROUP-ISOLATION.md).

## Verification and closure

- Use loader validation, C ABI RenderPlan tests, and software raw-pixel gates
  for each implemented behavior. Inspect actual preview PNGs; command success
  alone is insufficient.
- Run the focused SVG loader/C ABI/text-health suites and a full SVG-enabled
  build with `-j10`; report unrelated failures separately.
- Run the full acceptance workflow on a clean SHA in CI before claiming
  repository-level completion. Strict-native Vulkan remains a separate
  capability-dependent gate.
- If upstream code is reused, retain its license notice and identify the
  borrowed algorithm or test fixture. Do not link a Rust renderer.

## References

- [SVG 2 text specification](https://www.w3.org/TR/SVG2/text.html)
- [Web Platform Tests SVG text](https://github.com/web-platform-tests/wpt/tree/master/svg/text)
- [resvg test suite](https://github.com/linebender/resvg-test-suite)
- [usvg text layout source](https://github.com/linebender/resvg/blob/master/crates/usvg/src/text/layout.rs)
