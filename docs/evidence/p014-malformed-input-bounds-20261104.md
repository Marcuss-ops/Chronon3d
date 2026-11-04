# P0-14 malformed input and resource bounds — 2026-11-04

Focused local checks against the `linux-video-test` C3D build. These are
deterministic regression tests; they do not constitute a fuzzing run.

Passed C ABI cases:

- RenderPlan decoder returns a typed validation path for malformed input:
  1 case / 2 assertions.
- RenderPlan layer-count budget: 1 / 2.
- RenderPlan pixel, frame, layer and memory limits: 1 / 8.
- RenderPlan resolution, FPS and duration limits: 1 / 8.
- Inline mesh aggregate-byte limits and structural invariants: 1 / 8.
- Fail-loud RenderPlan budget phase: 1 / 5.
- Inline mesh malformed geometry and non-triangle data: 1 / 14.
- Lottie malformed documents and resource bounds with typed errors: 1 / 32.
- Lottie schema malformed/root and invalid cue coverage is included in the
  same targeted importer case.

All executed cases passed. The complete glTF malformed-asset corpus, bounded
fuzzing across the RenderPlan/glTF/asset boundaries, and explicit process-level
OOM/resource exhaustion checks have not been run. Status remains PARTIAL.
