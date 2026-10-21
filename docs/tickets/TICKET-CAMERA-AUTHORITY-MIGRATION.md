# Camera Authority Migration — CM3D authoring, C3D rendering

Status: `ACTIVE`

## Decision

ChrononMotion3D (CM3D) is the sole owner of cinematic camera authoring and
evaluation: pose, look-at, orbit, dolly, framing, transitions, FOV/lens,
focus, shake, and shutter-time camera samples. Chronon3D (C3D) consumes the
resolved camera and remains responsible for 2.5D projection, depth, pixel DOF,
motion-blur accumulation, masks, compositing, and output.

Do not remove C3D camera-authoring code until every production consumer has
migrated and the gates below pass. `Camera2_5D` may remain as renderer state.

## Current consumer census

Remaining production paths outside `camera_v1/` include:

- Scene and timeline authoring expose `CameraDescriptor`, `PoseTracksSource`,
  `CameraProgram`, and `ShotTimeline` through `SceneBuilder`, `Scene`, and
  `TimelineBuilder` public headers and implementations.
- CLI visual canaries now submit already-resolved `Camera2_5D` values instead
  of creating Camera V1 descriptors; their scene, hierarchy, and render probes
  still exercise the renderer's camera-consumer boundary.
- The unused `camera_motion::*` preset implementation and header have been
  removed: repository-wide search found no production, test, or tool call
  sites, and the header was not part of the SDK public-header manifest.
- The unused `CatmullRomCameraMotion` camera evaluator has been removed. Its
  camera-specific wrapper had no production callsites; CM3D already owns the
  Catmull-Rom `MotionPath`, path tangent/orientation, and `CameraRig` sampling.
  C3D retains the generic `CatmullRomPath3D` math for non-camera geometry.
- The unused `CameraMotionPath` evaluator has also been removed. C3D retains
  `SpatialBezierPath` and the quaternion math still consumed by Camera V1;
  CM3D `MotionPath` owns path sampling and `CameraRig` owns camera evaluation.
- The unused legacy `fit_camera_to_layers()` solver has been removed. It had
  no production callers; the separate Camera V1 `CameraFramingSolver` remains
  active and is still part of the pending migration to CM3D.
- Static and animated RenderPlan cameras now resolve directly to `Camera2_5D`
  renderer state. The RenderPlan execution path no longer creates Camera V1
  descriptors or calls its pose-track evaluator. Its generic camera tracks are
  sampled at the requested presentation time, including fractional shutter
  times; Composition/Timeline authoring still uses Camera V1 pending consumer
  migration.
- RenderPlan V3 `camera_shake` was removed. The decoder rejects plans carrying
  it; CM3D shake presets author the movement onto CameraRig tracks, which C3D
  consumes without a procedural shake evaluator.

This is an initial in-repository census, not proof that external SDK consumers
are absent. Before deleting public APIs, search SDK examples, install consumers,
CLI integrations, Template, and downstream source repositories.

## Migration slices

1. Freeze new cinematic camera features in C3D. Fixes to projection, depth,
   optical pixel effects, compatibility, and rendering remain in scope.
2. Specify the canonical camera-value contract: coordinate handedness/forward
   axis, position and focus units, quaternion convention, FOV/focal precedence,
   sensor size, clipping planes, timebase, and shutter samples.
3. Keep the current RenderPlan bridge for frame samples; make its adapter the
   sole coordinate conversion point and certify integer and sub-frame parity.
4. Move shake and any missing framing/trajectory constraints to CM3D, migrating
   Template consumers to the CM3D resolver.
5. Retire C3D preset/program/timeline evaluators in small slices only after
   consumer census reaches zero for the removal scope.

## Acceptance gates

- Static CM3D camera lowers to the expected C3D projection.
- Orbit and dolly across five depth layers preserve parallax without jumps.
- Equivalent FOV and 50 mm lens intrinsics project identically.
- Rack focus selects the expected near/far planes and changes rendered pixels.
- DOF off/on has visible pixel difference without alpha halos.
- 180-degree shutter uses CM3D camera samples at sub-frame times.
- Random-access frame 135 matches sequential frame 135 on one backend.
- Old and migrated paths match on their shared supported cases.
- Runtime, memory, CPU/GPU, and fallback behavior meet the agreed production
  envelope before deleting the compatibility path.

## Demolition debt

- **Owner:** CM3D camera and C3D rendering maintainers jointly; CM3D owns the
  authored/evaluated camera contract, C3D owns its renderer consumer.
- **Reason:** C3D still evaluates camera descriptors/tracks and applies shake
  while Template is migrating to CM3D CameraRig/CameraMotion.
- **Exit condition:** all in-repository and supported SDK consumers submit
  resolved CM3D camera samples; all acceptance gates pass on a same-SHA build.
- **Equivalence test/gate:** camera projection parity, pixel DOF/rack-focus,
  shutter sampling, random-access, and old/new comparison above.
- **Removal scope:** C3D cinematic presets, trajectory/shot authoring,
  `CameraProgram` evaluation for migrated consumers, and C3D-side shake
  evaluation. Preserve projection, `Camera2_5D` renderer state, depth, DOF
  kernels, motion-blur accumulation, and compositing.
- **Status:** `ACTIVE`.
