# Camera Authority Migration — CM3D authoring, C3D rendering

Status: `ACTIVE` — Camera V1 source and SDK authoring surface removed from C3D;
Template migration and end-to-end gates remain open.

## Decision

ChrononMotion3D (CM3D) is the sole owner of cinematic camera authoring and
evaluation: pose, look-at, orbit, dolly, framing, transitions, FOV/lens,
focus, shake, and shutter-time camera samples. Chronon3D (C3D) consumes the
resolved camera and remains responsible for 2.5D projection, depth, pixel DOF,
motion-blur accumulation, masks, compositing, and output.

`Camera2_5D` and evaluated projection remain renderer state. C3D Camera V1 has
been removed; the remaining work is to finish Template migration and certify
the CM3D-to-C3D bridge and rendered pixels against the gates below.

## Current consumer census

- The Camera V1 evaluator, descriptors, session cache, shot timeline, and
  public headers have been removed from C3D. Runtime consumers must submit
  resolved `Camera2_5D` renderer state or use the CM3D RenderPlan adapter.
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
- Camera trajectory, framing, constraints and timeline evaluation belong to
  CM3D; C3D retains generic path and quaternion math where used by rendering.
- Static and animated RenderPlan cameras now resolve directly to `Camera2_5D`
  renderer state. The RenderPlan execution path no longer creates Camera V1
  descriptors or calls its pose-track evaluator. Its generic camera tracks are
  sampled at the requested presentation time, including fractional shutter
  times. Composition/Timeline no longer exposes the retired Camera V1 API.
- RenderPlan V3 `camera_shake` was removed. The decoder rejects plans carrying
  it; CM3D shake presets author the movement onto CameraRig tracks, which C3D
  consumes without a procedural shake evaluator.
- An optional runtime camera-sample provider now crosses the RenderPlan compile
  boundary. CM3D evaluates exact fractional and pre-roll shutter times; its
  provider identity salts both render-job and compiled-scene cache identities.
  ChrononTemplate exposes `lowerToRuntimeRenderPlan`, which returns the layer
  plan without dense camera tracks plus an immutable CM3D rig provider. Offline
  JSON plans keep the existing baked-track path until their consumers migrate.
- The runtime-provider contracts pass their focused checks: C3D verifies exact
  fractional and negative shutter sampling plus cache identity; CM3D verifies
  rig lowering; Template verifies runtime lowering omits camera tracks.

The C3D source and public header trees have been searched. This does not prove
that external SDK consumers are absent; downstream consumers should be checked
before release, and Template still needs full migration to the CM3D recipe and
sequence APIs.

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
5. Complete Template migration and the image-level acceptance gates; audit
   supported downstream SDK consumers before closing this migration.

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
- **Reason:** Template still has camera choreography and selection adapters to
  converge on CM3D, and the end-to-end camera contract has not passed all pixel
  and shutter-time acceptance gates.
- **Exit condition:** all in-repository and supported SDK consumers submit
  resolved CM3D camera samples; all acceptance gates pass on a same-SHA build.
- **Equivalence test/gate:** camera projection parity, pixel DOF/rack-focus,
  shutter sampling, random-access, and old/new comparison above.
- **Removal scope:** C3D cinematic presets, trajectory/shot authoring,
  `CameraProgram` evaluation for migrated consumers, and C3D-side shake
  evaluation. Preserve projection, `Camera2_5D` renderer state, depth, DOF
  kernels, motion-blur accumulation, and compositing.
- **Status:** `ACTIVE`.

## Offline camera-track closeout (2026-10-21)

The in-repository `camera_animation` path has been removed. CM3D now emits
complete `camera_samples` snapshots from `CameraRig`; C3D validates, fingerprints,
and consumes those snapshots. Legacy plans carrying `camera_animation` fail
closed with `camera_animation_retired`. The previous file-level census is
closed as follows:

| Surface | Closeout |
|---|---|
| C3D RenderPlan model, V2/V3 schemas, decoder, validator | Replaced track types and schema with ordered `camera_samples`; old field rejected |
| C3D camera compiler and temporal-rate analysis | Reads complete camera snapshots; no property-track evaluator remains |
| C3D fingerprinting | Hashes ordered sample frames and full camera values |
| CM3D lowering and JSON emission | `bakeResolvedCameraSamples` writes full per-frame camera state; runtime provider remains the exact-time in-process route |
| C3D preview generator and delivery verifier | Generate and inspect resolved camera samples |
| Test fixtures and saved plans | Converted to `camera_samples`, including text stress, residency, SVG motion, scene, and Apollo plans |
| C3D/CM3D decoder, lowering, DOF, temporal-rate, round-trip, and geospatial tests | Rewritten around complete camera samples |
| Camera2_5D canary | Parent yaw is resolved into the submitted camera pose before rendering |

The retired model symbols, legacy schema definitions, producer option, camera
property writer, decoder, fingerprint fields, evaluator, and fixture generator
path have been deleted. `camera_animation` remains only in the decoder's
explicit rejection branch and its rejection test.

Offline `camera_samples` are generated at integer presentation frames. C3D
linearly interpolates complete resolved poses between adjacent snapshots when
it evaluates a fractional time. Runtime-provider sampling is exact at requested
sub-frames. This keeps cinematic curves and look-at evaluation in CM3D, but
pixel parity between offline interpolation and direct CM3D sampling at shutter
sub-frames is still an open certification item, along with G1–G8 and T16–T18.
The overall camera migration therefore remains `ACTIVE` until those gates pass.

Demolition debt for the legacy track representation is `REMOVED`: the producer,
serialized field, schema, decoder, fingerprint logic, and evaluator are gone.
The remaining active debt is end-to-end sub-frame and optical certification,
not a second serialized camera-animation path.

The CLI canary had a stale camera-parent assignment after `Camera2_5D` became
resolved renderer state. It now composes the parent yaw into the orbit pose
before calling `set_evaluated_camera()`, retaining the canary motion without
camera parenting in the renderer.

## Follow-up audit (2026-10-21)

The C3D decoder migration does not close the whole workspace migration. A new
workspace-wide search found remaining ChrononTemplate command-line, Python,
and Node plan generators that still emit `camera_animation`, including camera
roll, single-image, phrase, map, documentary, and NBA tools. Those generated
plans are rejected by current C3D and must be migrated to CM3D-generated
`camera_samples` or retired with their output references. Nine checked-in
Template golden plans also still contain the retired field. The C++ web-page,
article-evidence, and blackboard producers now bake complete resolved samples
through CM3D, but this is only a partial producer closeout.

CM3D follow-up changes remove the ambiguous `aperture` aliases (the remaining
normalized legacy blur gain is explicitly named `legacyDefocusStrength`) and
route shot/sequence/environment focus distances through `CameraFocusResolver`
using optical-axis distance. The offline baker is exposed as the canonical
CM3D `bakeResolvedCameraSamples` helper and reused by Template.

The overall migration remains ACTIVE. Remaining non-visual, non-CI work:

- migrate or retire every active Template generator and regenerate or remove
  its stale serialized plans;
- complete the shared offline/runtime `CameraSample` contract (including the
  authored sensor/lens-shift and time/shutter fields still absent from the
  RenderPlan camera payload), or explicitly narrow the accepted contract;
- inventory downstream consumers and ensure each consumes resolved samples;
- complete the same-meaning FOV/focal-length precedence and optical focus
  contract across the core rig, RenderPlan bridge, and render backends.

The G gates, image-level parity checks, and GitHub CI are tracked separately
and are not represented as complete by this audit.

## Workspace closeout progress (2026-10-21)

Partial implementation is in the working trees and is not yet a closeout:

- The shared `CameraPlan` now carries `sensor_width_mm` and normalized
  `lens_shift` (`+X` right, `+Y` down). CM3D's offline baker and JSON bridge
  serialize both; C3D decodes, validates, compiles, and fingerprints them.
- Camera sample time is defined by absolute `frame` and canvas `fps_num/fps_den`.
  Shutter duration is `(shutter_angle / 360) * (fps_den / fps_num)` seconds;
  shutter sample count is carried by `MotionBlurPlan`. The runtime provider
  evaluates exact requested times; offline samples interpolate complete poses.
- Several CameraRig-based Template emitters now use CM3D's resolved sample
  baker. The scene-camera and documentary snapshot Python adapters now serialize
  already-resolved per-frame poses rather than property tracks. The title-camera
  v1 Python generator was retired in favor of the v2 C++ emitter.
- The active blackboard golden was regenerated from its current emitter. Eight
  unreferenced signature/astronaut golden plans were removed; they had no
  repository consumers.

Still open: multiple active Template producers continue to emit or stage
`camera_animation`. The earlier note that the current edits were uncommitted
and the C3D build interrupted is historical; Template `main` now contains
`3872349 Migrate camera emitters to resolved samples`. The current producer
inventory and remaining plan are recorded below. Do not mark this ticket
complete until those producers are migrated or retired and focused generation
and decode checks pass. G-gates, visual parity, and CI remain outside this
closeout as requested.

## Workspace inventory refresh (2026-10-21, after Template `3872349`)

At the current workspace revisions, no checked-in `*.plan.json` golden
contains the retired field, and CM3D production code no longer emits it. The
remaining Template matches split into actual C3D plan producers, internal
map-motion selectors, tests, and one stale comment:

| Action | Files | Reason / exit check |
|---|---|---|
| `MIGRATE` | `tools/web_presets/build_nba_dirk_physical_fold.mjs`; `tools/web_presets/emit_synthetic_cannabis_native.cpp`; `tools/web_presets/emit_dark_nba_article_native.cpp`; `tools/single_images/generate_image_25d_clean_v1.cpp`; `tools/nba_rivalry/build_precomp_2.mjs`; `tools/nba_rivalry/build_timeline_10.mjs`; `tools/camera_roll/emit_camera_1950_story.cpp`; `tools/entities_with_text/emit_cinematic_documentary_narrative.cpp`; `tools/important_phrases/build_text_depth_focus_v1.py`; `tools/important_phrases/render_editorial_didone_canary.py`; `tools/short_phrases/emit_short_phrase_plans.cpp`; `tools/documentary_effects/emit_beast_land_subject_reframe.cpp` | These construct serialized C3D plans with property tracks. Route their authored camera through CM3D `CameraRig` and `bakeResolvedCameraSamples`, or retire the generator and its consumers. Output must contain `camera_samples`, never `camera_animation`, and decode with current C3D. |
| `KEEP` (rename separately) | `tools/map/render_map_editorial_pack_v1.py`; `tools/map/render_dynamic_map_overlay.py` | Here `camera_animation` is a map recipe selector (`slow_approach`), not the retired RenderPlan object. Rename it to `camera_move` in the map request/manifest contract; do not feed it into C3D plans. |
| `UPDATE` | `tools/camera_roll/dump_scene_camera_poses.cpp` | Its comment says the CSV is lowered into `camera_animation`; the current Python consumer creates resolved `camera_samples`. Correct the documented contract. |
| `KEEP` | `tests/web_page_highlight_pack.cpp`; `tests/web_text_layout.cpp`; `tests/native_primitives.cpp` | These assert the `camera_samples` contract and absence of the retired field; they are regression coverage, not producers. |

The C3D occurrences are limited to the decoder's explicit fail-closed branch,
its rejection test, and migration documentation. CM3D's only source match is a
test assertion that emitted JSON omits `camera_animation`. Therefore the
remaining serialized producer closeout is in Template. The golden sweep is
closed for checked-in plan goldens, but it does not certify runtime-generated
plans.

Exit the offline producer closeout only after every `MIGRATE` entry has a
focused generation/decode check (or is removed with its consumers), the map
selector contract is clarified, the stale comment is fixed, and a workspace
search finds no production producer of the retired serialized field. Keep
G1–G8, T16–T18, and same-SHA CI as separate acceptance work.
