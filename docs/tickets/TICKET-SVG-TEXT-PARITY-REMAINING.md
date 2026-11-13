# Ticket — Remaining SVG text support

**Status:** P0 and P1 closed locally (2026-11-12); P2 open; same-SHA CI not run.
**Parent:** [TICKET-SVG-RENDER-PLAN.md](TICKET-SVG-RENDER-PLAN.md)
**Handoff baseline:** remote `main` at `a204d0d6` (2026-10-21); recheck the
current `main` head before implementation because the other workstation may
have advanced it.

## TODO — cosa manca (agg. 2026-10-28)

- [x] **P0 — `textLength`/`lengthAdjust` + spacing/rotate cluster-aware** — logica: vincoli annidati inner-to-outer su `<text>`/`<tspan>`, `letter-spacing`/`word-spacing`/`rotate` ereditati con comportamento cluster-aware (mai splitting silenzioso di ligature/grapheme); definire le combinazioni con style run, whitespace, coordinate list e anchor. Dove: importer SVG text + shaping HarfBuzz + layout. Gate: loader/C ABI + raw gates (larghezze parent/child misurate separatamente, due mode `lengthAdjust` confrontati, cluster split fail-closed, repeat byte-identici).
- [x] **P1 — contratto pattern-paint bounded completo** — chiuso localmente (2026-11-12): gruppi ricorsivi, gradient/stroke nel tile, `objectBoundingBox`, `href` con cicli; transform/alpha/fill rules/phase preservati e coperti da fixture. Evidenze sotto.
- [x] **P1 — TextPath shaping + attributi residui** — chiuso localmente (2026-11-12): chunk compatibili fusi prima dello shaping, paint per range applicato per cluster, RTL senza doppia inversione, `method`/`spacing`/`side` espliciti. Evidenze sotto.

## P0 closure evidence (2026-11-12)

**Local state:** base HEAD `f126967f4`; the tree is dirty and the changes are
not committed. This is not a same-SHA CI certification.

### Implemented

- Parser: `SvgTextLengthScope` on `<text>`/`<tspan>`; `textLength` belongs to
  the declaring element and is not inherited; nested `lengthAdjust` is
  preserved; nested per-character `rotate` remains fail-closed.
- Compiler: `resolve_svg_text_length_scopes` applies constraints inner-to-outer,
  handles `spacing` and `spacingAndGlyphs`, internal/boundary gaps, resolved
  metrics and anchors; cluster lowering uses the resolved scale/gap.
- CLI/render path: `compiler::compile` now calls `prepare_svg_layers`, so the
  canonical CLI route expands SVG path/text layers like the C ABI route. Before
  this, the CLI rendered SVG assets as empty frames.
- Placement: SVG text layers are converted from authored top-left to the
  renderer's centre-box convention; merged paint runs recentre on the combined
  box; `spacingAndGlyphs` cluster layers compensate frame-scale growth.
- Raw gate: removed the duplicate `fill` that made direct children black, and
  moved the fixture to an 800x160 canvas at x=160 so direct and nested cases are
  not clipped by the left edge.

### Verification

```text
cmake --build build/chronon/linux-video-release \
  --target chronon3d_svg_path_loader_tests chronon3d_c_abi_tests chronon3d_cli -j12
BUILD_EXIT=0

CHRONON_CLI=build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli \
  python3 scripts/ci/check_svg_text_spacing_raw_gate.py
RAW_GATE_EXIT=0
length_spacing=354 length_glyphs=353 direct_400=394 direct_400_glyphs=393
direct_child_250=244 direct_child_250_glyphs=241
nested_parent_spacing=394 nested_parent_glyphs=393 nested_both=414
red: child_plain=120 nested_child_spacing=244 nested_child_glyphs=240 nested_both=234
nested_both and rotate repeat frames: byte-identical

chronon3d_svg_path_loader_tests: 55/55 cases, 444/444 assertions
C ABI nested textLength: 3/3 assertions
C ABI fail-closed: textLength+coordinate list, textLength+textPath and
  coordinate-list cluster split: 3/3 assertions
```

Preview PNGs inspected from `/tmp/svg_preview_p0_final/`:

```text
plain:                  black (162,61)-(421,108)
child_plain:            black (162,62)-(280,108), red (302,61)-(421,108)
nested_child_spacing:   black (162,62)-(280,108), red (302,61)-(545,108)
nested_child_glyphs:    black (162,62)-(280,108), red (303,61)-(542,108)
direct_child_250_glyphs: red   (162,61)-(402,108)
nested_both:            black (162,61)-(299,108), red (342,61)-(575,108)
```

The nested child starts after the parent/space in both `lengthAdjust` modes;
no overlap and no left-edge clipping remain.

### Residual / not closed

- The stale C ABI fixture index 9 (a path-level `animateTransform` inside a
  translated group) has moved to the supported set with frame 0/frame 30
  geometry assertions. Full `chronon3d_c_abi_tests`: 500/500 cases and
  10,959/10,959 assertions pass locally on the current dirty tree.
- Same-SHA CI was not run (feature-freeze rule) and no commit was made.
- P1 is closed locally (see below). P2 remains: vertical writing/BASE
  metrics and SVG text inside isolated groups.

## P1 closure evidence (2026-11-12)

**Local state:** base HEAD `f126967f4`; the tree is dirty and the changes are
not committed. This is not a same-SHA CI certification.

### Pattern paint

- Pattern import is two-phase: gradients are collected first, then pattern
  `href` chains are resolved and tiles rasterized, so paint definitions may
  appear in any document order.
- Tile content supports recursive `<g>` (depth 8), group/shape opacity
  isolation, solid/gradient fill, solid/gradient stroke, `fill-rule`, and
  child/group transforms. Bounds: 32 shapes, 1024 commands per shape, 4096
  total commands.
- `patternUnits`/`patternContentUnits` support matching `userSpaceOnUse` pairs
  and matching `objectBoundingBox` pairs. Mixed units, `viewBox`,
  `preserveAspectRatio`, nested pattern paint, animated gradient paint, dash
  strokes, and dangling/cyclic `href` chains fail closed.
- `PatternTile` object-bounding-box metadata participates in the text-run hash
  and render-plan fingerprint.
- Verification:

```text
loader: 59/59 cases, 486/486 assertions
C ABI focused pattern: 1/1 case, 17/17 assertions
pattern raw gate: PASS fill/stroke, phase, groups/gradient/stroke/href/
  objectBoundingBox, and repeat determinism
```

### TextPath

- Compatible `textPath` chunks (same font/direction/path/anchor and no
  positioning) are merged before shaping. Paint changes are retained as byte
  ranges and applied per shaped cluster, so style boundaries do not split
  ligatures or joining runs.
- `direction=rtl` and auto detection place shaped clusters in visual order
  without double reversal; explicit LTR keeps logical order.
- Supported attribute subset is explicit: `method=align`, `spacing=exact`,
  `side=left`; every other value fails closed. Animated/external path targets
  remain fail-closed.
- Verification:

```text
loader textPath merge: 1 run, 2 paint ranges
C ABI focused textPath: 3/3 cases, 28/28 assertions
composer: 21/21 cases, 68/68 assertions (includes RTL explicit/auto)
textPath raw gate: LTR/RTL progression, explicit/auto equality, and repeat
  determinism PASS
P0 spacing raw gate: PASS (no regression)
```

### Residual

- C ABI fixture 9 was stale because the nested path animation is supported.
  It is now asserted as a positive start/end-frame case; the full C ABI suite
  passes 500/500 cases and 10,959/10,959 assertions on the current dirty tree.
- Same-SHA CI was not run and no commit was made.
- P2 remains: vertical writing, positive BASE-metrics fixture, and SVG text in
  isolated groups.

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
- Bounded pattern text paint: `userSpaceOnUse` and matching
  `objectBoundingBox` tiles, recursive groups, solid/gradient fill and stroke,
  local `href` inheritance with cycle checks; raw gates cover fill/stroke,
  phase, bounds, alpha and repeats.
- Ordinary text `x`, `y`, `dx`, and `dy` coordinate lists, including paired
  coordinates under rotated/skewed transforms.
- Static local `textPath` references with `startOffset`, `text-anchor`,
  `x`/`y`/`dx`/`dy` coordinate lists, projection into transformed path
  coordinates, shaping across compatible paint chunks, and RTL visual-order
  placement. Existing gates verify software pixels and repeat determinism.

## Open work

### P0 — Text advance and character-position attributes — CLOSED LOCALLY (2026-11-12)

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
  preview PNGs, and confirm byte-identical repeat frames. **Stato:** soddisfatto
  localmente; vedi "P0 closure evidence" sopra. I limiti residui sono nella
  stessa sezione (C ABI full suite e CI same-SHA).

### P1 — Complete the bounded pattern-paint contract — CLOSED LOCALLY (2026-11-12)

- Extend pattern children to cover groups and supported gradient/stroke paint
  inside a tile.
- Add `objectBoundingBox` tile units and `href` inheritance with cycle and
  unsupported-reference checks.
- Preserve transforms, alpha composition, fill rules, and fill/stroke pattern
  phase through the existing paint/cache/fingerprint paths.
- **Accept when:** loader/C ABI tests cover each supported pattern form and
  each rejection; raw fill/stroke gates inspect tile phase, bounds, alpha, and
  repeated-frame equality.

### P1 — TextPath shaping and remaining SVG attributes — CLOSED LOCALLY (2026-11-12)

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
