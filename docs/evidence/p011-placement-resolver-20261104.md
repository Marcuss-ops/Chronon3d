# P0-11 immutable placement resolver — 2026-11-04

Focused local verification on C3D `e2283379c`, using the `linux-video-test`
build and only the render-graph test target.

Passed coverage:

- `*EvaluatedLayerPlacement*`: 3 cases / 30 assertions. Canvas, projected and
  native placement select one immutable matrix owner and do not apply a second
  transform.
- `Canonical placement: headline subtitle and watermark resolve offsets once`:
  1 case / 12 assertions.
- `SourceNode predicted_bbox vs execute*`: 5 cases / 50 assertions, covering
  2D, centered and camera-space sources.
- Placement/bounds diagnostic invariance: 1 case / 24 assertions.
- Camera2.5D multi-source cold/warm frame equivalence: 1 case / 3 assertions.

Total: 11 cases / 119 assertions passed. Build/refresh/render placement is
resolved through `EvaluatedLayerPlacement`; predicted and executed bounds,
diagnostic mode and cold/warm rendering agree on the tested fixtures. This is
local code evidence, not same-SHA CI certification.
