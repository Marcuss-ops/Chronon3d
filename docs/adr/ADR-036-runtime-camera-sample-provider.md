# ADR-036 — Runtime camera samples from the motion authority

**Status**: ACCEPTED (runtime provider implemented; consumer migration in progress)

**Date**: 2026-10-21

**Deciders**: Chronon3D and ChrononMotion3D maintainers

**Tags**: `camera`, `authority`, `render-plan`, `shutter`, `demolition-debt`

## Context

ChrononMotion3D (CM3D) owns camera authoring and `CameraRig::sample(time)`.
Chronon3D (C3D) owns projection, depth, pixel DoF, and shutter accumulation.
The current offline bridge samples the rig at integer frames into dense linear
`camera_animation` tracks. C3D evaluates those tracks at the render time,
including fractional times requested by shutter accumulation.

That representation is deterministic and useful for standalone RenderPlans,
but linear interpolation between integer-frame samples is not guaranteed to
equal `CameraRig::sample()` at an arbitrary shutter time on curved or eased
motion. Putting CM3D curves or easing into the C3D evaluator would restore a
second camera authority. Making C3D guess the shutter schedule in CM3D would
duplicate renderer sampling policy.

## Decision

Add an optional, non-serialized runtime camera-sample provider at the
RenderPlan compilation boundary. The provider is invoked with the exact
presentation time requested by C3D and returns a fully resolved renderer
camera value. A provider may receive negative pre-roll times at the leading
shutter edge; CM3D track endpoint behavior resolves those samples. An integrated
CM3D caller implements it by calling
`CameraRig::sample(time)` and lowering that pose through the canonical
coordinate and optics adapter.

C3D continues to own the request times, shutter pattern, sample accumulation,
projection and pixel effects. It must not apply camera easing, look-at,
shake, trajectory, or other authoring rules to a provider result.

Serialized RenderPlans remain self-contained and continue to use their static
camera and baked linear tracks when no runtime provider is supplied. The
provider path takes precedence when supplied. It is not embedded in plan JSON
or the plan content fingerprint. Its caller supplies a stable provider
fingerprint, which participates in the prepared-job fingerprint so distinct
camera sources cannot collide in caches.

The provider returns the existing renderer-facing `CameraPlan` value. Its
identity contributes to both prepared-job and compiled-scene cache keys, but is
not embedded in the serialized plan content fingerprint. A separate public
`CameraFrame` schema is deferred until coordinate, matrix,
sensor, focus, timebase, and clipping conventions have been certified across
all supported backends.

## Consequences

- Provider-backed plans use CM3D for every runtime camera sample, including
  shutter subframes. Plans without a provider retain their serialized track
  evaluation until supported consumers migrate.
- C3D's temporal sampler remains the sole owner of shutter timing and
  accumulation.
- Existing serialized plans preserve their current behavior during migration.
- Runtime provider identity becomes part of job/cache identity and must be
  deterministic and stable for the same authored rig.
- The compatibility camera-track evaluator remains until consumer census,
  visual parity, random-access, and same-SHA gates pass.

## Demolition debt

- **Owner:** CM3D motion maintainers own the provider implementation; C3D
  render-plan maintainers own the callback boundary and compatibility path.
- **Reason:** serialized legacy plans and external SDK consumers still need
  baked tracks while runtime integrations migrate.
- **Exit condition:** all supported in-repository and SDK consumers supply
  resolved CM3D camera samples; migration acceptance gates pass on one SHA.
- **Equivalence test/gate:** compare provider samples against `CameraRig` at
  integer and sub-frame times; compare rendered shutter output, random-access
  frames, and supported legacy plans.
- **Removal scope:** C3D camera-track policy/easing evaluation and any
  camera-specific compatibility evaluator no longer used by supported plans.
  Preserve static renderer camera values, projection, depth, DoF kernels,
  shutter scheduling/accumulation, and compositing.
- **Status:** `ACTIVE`; runtime-provider migration is implemented in C3D,
  CM3D, and Template. Consumer migration and acceptance gates remain open.

## Alternatives considered

1. **Bake a denser fixed sub-frame grid.** Rejected as a final contract:
   arbitrary shutter schedules would still interpolate between samples, with
   a resolution/quality tradeoff and larger plans.
2. **Move shutter-time generation into CM3D.** Rejected: shutter sampling and
   accumulation belong to the renderer, and duplicating its sample pattern in
   the motion library would create another timing authority.
3. **Put CameraRig evaluation into C3D.** Rejected: it creates a build/runtime
   dependency in the wrong direction and restores duplicate cinematic policy.
4. **Keep only integer-frame parity.** Rejected: it does not prove the
   sub-frame camera poses needed for motion blur.

## References

- [`TICKET-CAMERA-AUTHORITY-MIGRATION`](../tickets/TICKET-CAMERA-AUTHORITY-MIGRATION.md)
- [`ADR-029-authority-boundary-doctrine`](ADR-029-authority-boundary-doctrine.md)
