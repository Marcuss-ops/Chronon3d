# ADR-036 — Runtime camera samples from the motion authority

**Status**: ACCEPTED (runtime provider and serialized resolved samples implemented; image-level certification remains open)

**Date**: 2026-10-21

**Deciders**: Chronon3D and ChrononMotion3D maintainers

**Tags**: `camera`, `authority`, `render-plan`, `shutter`, `demolition-debt`

## Context

ChrononMotion3D (CM3D) owns camera authoring and `CameraRig::sample(time)`.
Chronon3D (C3D) owns projection, depth, pixel DoF, and shutter accumulation.
The offline bridge serializes complete `camera_samples` resolved by CM3D at
output-frame times. C3D reads those complete poses and linearly interpolates
between adjacent resolved samples when a renderer requests an intermediate
presentation time. Runtime provider calls continue to sample CM3D directly at
the exact requested time, including shutter subframes.

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

Serialized RenderPlans remain self-contained and use `camera_samples` when no
runtime provider is supplied. The provider path is mutually exclusive with the
serialized sample list. It is not embedded in plan JSON or the plan content
fingerprint. Its caller supplies a stable provider fingerprint, which
participates in the prepared-job fingerprint so distinct camera sources cannot
collide in caches.

The provider returns the existing renderer-facing `CameraPlan` value. Its
identity contributes to both prepared-job and compiled-scene cache keys, but is
not embedded in the serialized plan content fingerprint. A separate public
`CameraFrame` schema is deferred until coordinate, matrix,
sensor, focus, timebase, and clipping conventions have been certified across
all supported backends.

## Consequences

- Provider-backed plans use CM3D for every runtime camera sample, including
  shutter subframes. Offline plans serialize complete CM3D-resolved camera
  values; C3D has no camera property-track parser or per-property evaluator.
- C3D's temporal sampler remains the sole owner of shutter timing and
  accumulation.
- Serialized plans carrying the retired `camera_animation` field are rejected
  with a migration error. In-repository saved plans and fixtures now use
  `camera_samples`.
- Runtime provider identity becomes part of job/cache identity and must be
  deterministic and stable for the same authored rig.
- The only offline camera interpolation is linear interpolation between full
  resolved snapshots. Curved/sub-frame shutter parity against direct CM3D
  sampling remains a certification gate.

## Demolition debt

- **Owner:** CM3D motion maintainers own rig evaluation and offline sample
  generation; C3D render-plan maintainers own the provider/sample bridge and
  renderer consumption.
- **Reason:** sub-frame optical and shutter pixel parity has not yet been
  certified across the supported render paths.
- **Exit condition:** no track-based producer or evaluator remains, and the
  projection, DOF, shutter, random-access, and same-SHA acceptance gates pass.
- **Equivalence test/gate:** compare provider samples against `CameraRig` at
  integer and sub-frame times; compare rendered shutter output, random-access
  frames, and supported legacy plans.
- **Removal scope:** completed for `CameraTrackPlan`, `CameraPropertyPlan`,
  `camera_animation` schemas/decoding/emission/fingerprinting, and track-based
  camera sampling. Preserve `CameraPlan` renderer values, `camera_samples`,
  runtime provider calls, projection, depth, DoF kernels, shutter
  scheduling/accumulation, and compositing.
- **Status:** `ACTIVE`; legacy-format demolition is complete in C3D and CM3D.
  Pixel and shutter certification remains open.

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

- `TICKET-CAMERA-AUTHORITY-MIGRATION` (scheda chiusa e rimossa il 2026-10-27; storico in git)
- [`ADR-029-authority-boundary-doctrine`](ADR-029-authority-boundary-doctrine.md)
