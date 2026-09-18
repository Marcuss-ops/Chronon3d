# TICKET-GLOW-QUALITY-2026-09-18 — final glow quality and canary pixel regression

**Status**: `IN_PROGRESS`
**Scope**: Chronon3d software glow pipeline and Motion3D canary output.

## Problem

Final canary renders were using `GlowQuality::Fast` through the default `GlowParams` presets. The glow pipeline also had inconsistent falloff/ROI contracts and accumulated lobe coverage with `max(alpha)` while adding RGB.

## Changes

- Map final effect execution to `GlowQuality::High` without changing interactive preview behavior.
- Use non-boosting falloff for canonical cinematic presets.
- Keep the wide lobe at full resolution for final presets.
- Accumulate lobe coverage with source-over union semantics.
- Make the reported glow extent match the actual Gaussian ROI padding.
- Remove the dead blend-mode branch.

## Acceptance gates

- `chronon3d_effects_tests` passes completely.
- Glow-specific regression tests pass completely.
- All five Motion3D canary compositions render at 1920x1080, 30 fps, 90 frames.
- Golden/determinism/repeat/same-motion/H.264 checks pass after the intentional pixel change.
- Updated canary artifacts are retained under the new output directory; no old golden is silently overwritten.
