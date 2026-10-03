# Spatial MotionPath3D surface removal

Stato: **REMOVED**.

## Motivazione

`spatial_motion_path.hpp` and the `temporal_spatial_curve.hpp` umbrella had no
production callers. Their spatial path types duplicated the live path
vocabulary in ChrononMotion3D and the RenderPlan/camera path sampler in
Chronon3D, without participating in a production route. The source census was
limited to these headers and three tests.

## Rimozione

- Removed `CubicBezier3D`, `SpatialKeyframe3D`, `MotionSegment3D`,
  `MotionPath3D`, `MotionPathSampler3D`, and their umbrella header.
- Removed the three tests that exercised only those unused spatial types.
- Retained `TemporalCurve1D`, which is used by effect and layer production
  paths, and moved its five tests into `test_temporal_curve_1d.cpp` with a
  direct include of its owning header.
- ChrononMotion3D `MotionPath` and the RenderPlan camera path sampler remain
  independent authorities for their existing production contracts.

## Demolition debt

- Owner: Chronon3D animation/runtime maintainers.
- Reason: none; removal is complete.
- Exit condition: no production or public umbrella references remain.
- Equivalence gate: TemporalCurve1D focused tests, animation/core suite,
  architecture gate, and ChrononMotion3D path tests.
- Removal scope: complete; no compatibility shim is retained.
- Status: `REMOVED`.
