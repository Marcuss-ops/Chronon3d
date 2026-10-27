# Camera Authority Migration — CM3D authoring, C3D rendering

Status: `CLOSED` (2026-10-27) — C3D no longer owns camera authoring; CM3D
producer migration, Template lowering, and the in-workspace SDK census are
complete. Remaining visual canary work is optional and tracked separately in
`TICKET-CINEMATIC-COMPOSITION-CERTIFICATION.md`; same-SHA/CI is not a closeout
requirement.

## Decision

ChrononMotion3D (CM3D) is the sole owner of cinematic camera authoring and
evaluation: pose, look-at, orbit, dolly, framing, transitions, FOV/lens,
focus, shake, and shutter-time camera samples. Chronon3D (C3D) consumes the
resolved camera and remains responsible for 2.5D projection, depth, pixel DOF,
motion-blur accumulation, masks, compositing, and output.

`Camera2_5D` and evaluated projection remain renderer state. C3D Camera V1 has
been removed. Template lowering and the CM3D-to-C3D value bridge are complete;
optional rendered-shot reviews are tracked in the cinematic ticket.

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

The C3D source and public header trees have been searched. The available
workspace census is recorded in the producer closeout below; third-party
consumers outside this checkout remain outside that census.

## Closeout

The implementation objective is complete: camera authoring and evaluation live
in CM3D, Template uses the resolved-sample lowering path, and C3D consumes
resolved camera state. Focused value/adapter checks and the T16 RenderPlan
integration are recorded below. Optional shot-quality and codec-output reviews
are not blockers for this ownership migration; see the cinematic ticket.

## Historical implementation log (older ACTIVE notes are superseded)

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
sub-frames. Pixel parity between offline interpolation and direct CM3D sampling
was an open item at that checkpoint; optional shot-level parity is now tracked
separately from migration closeout above.

Demolition debt for the legacy track representation is `REMOVED`: the producer,
serialized field, schema, decoder, fingerprint logic, and evaluator are gone.

The CLI canary had a stale camera-parent assignment after `Camera2_5D` became
resolved renderer state. It now composes the parent yaw into the orbit pose
before calling `set_evaluated_camera()`, retaining the canary motion without
camera parenting in the renderer.

## Follow-up audit (2026-10-21)

The follow-up inventory has now been closed for the active in-workspace
RenderPlan producers. Camera-roll, single-image, phrase, documentary, web-page,
article, blackboard, and NBA generators emit resolved `camera_samples`
directly or invoke the canonical CM3D camera-plan baker before writing the
final plan. The apparent `camera_animation` references in the map tools are
map-renderer style identifiers in their own payload, not RenderPlan camera
tracks. On 2026-10-21 the baker migrated 79 existing generated RenderPlans
under the local Template `out/` tree; the CLI validated 96 sample-bearing plans
across their asset roots (95 via the workspace roots and the Dirk fold plan via
its local generated asset directory). A fresh inventory of 132 RenderPlan
outputs found 96 with resolved samples and zero with `camera_animation`.
These `out/` artifacts are ignored generated files; the producer changes and
ticket record are the maintained source of truth.

CM3D follow-up changes remove the ambiguous `aperture` aliases (the remaining
normalized legacy blur gain is explicitly named `legacyDefocusStrength`) and
route shot/sequence/environment focus distances through `CameraFocusResolver`
using optical-axis distance. The offline baker is exposed as the canonical
CM3D `bakeResolvedCameraSamples` helper and reused by Template.

The overall migration remains ACTIVE. Remaining non-visual, non-CI work:

- certify the shared offline/runtime `CameraSample` values end-to-end. The
  current payload carries complete resolved poses, FOV/focal length, sensor
  width, lens shift, focus/DoF values and absolute presentation frame; seconds
  derive from the plan frame rate and shutter timing remains in the motion-blur
  contract. Offline plans interpolate complete poses; the CM3D provider samples
  exact requested times;
- inventory downstream SDK consumers outside the available workspace when
  those supported consumers are identified;
- finish optical and pixel parity gates across CM3D, the RenderPlan bridge and
  C3D backends. Focal length is authoritative when authored; otherwise FOV is
  authoritative. Focus distance is camera-space optical-axis depth and is
  explicitly converted for the legacy world-Z DOF consumer.

## G6 sub-frame parity follow-up (2026-10-22)

C3D now has a focused regression in
`tests/c_abi/test_prepared_render_plan.cpp` comparing offline interpolation of
integer-frame `camera_samples` against a runtime-provider callback implementing
the same linear camera motion at six fractional shutter sample times. CM3D's
actual `makeCameraSampleProvider` is separately exercised against `CameraRig`
sub-frame samples in `ChrononMotion3D/tests/motion_camera_bake_roundtrip.cpp`.
Together these cover the provider contract and C3D interpolation, but they do
not link the two implementations in one end-to-end sub-frame pixel test. This check
exposed missing interpolation for lens shift, sensor dimensions, and highlight
gain in the existing C3D resolver; those continuous fields now use that same
resolver's interpolation path. The focused G6 case passes with 165 assertions;
the full C3D camera/C ABI/motion-adapter CTest selection passes 3/3. This proves
camera-value parity for linear segments only, not a cross-engine sub-frame
pixel test. It does not close the 180-degree shutter pixel gate, non-linear
easing error, or full exposure accumulation certification.
T16/T17/T18 status remains as recorded in the cinematic composition
certification ticket.

The G gates, image-level parity checks, and GitHub CI are tracked separately
and are not represented as complete by this audit.

### Current C3D consumer verification (2026-10-22)

- `cmake --build build/chronon/linux-video-test --target chronon3d_cli -j12`:
  PASS, 413/413 Ninja tasks.
- CTest targets `chronon3d_camera_tests`, `chronon3d_c_abi_tests`, and
  `chronon3d_motion_plan_adapter_tests`: PASS, 3/3 (291.79 seconds total).
- The Motion3D→C3D bridge consumer regression was rebuilt (`-j12`, 303/303) and
  rerun: 23 checks, 0 failures.
- The current `ChrononTemplate/out` census contains 118 plans with
  `camera_samples`; C3D CLI `validate` passes all 118 with their asset roots.
- ChrononMotion3D `release-fast`: rig/camera/focus/trajectory/framing/sequence,
  RenderPlan easing and projection parity, bake round-trip, T16 and T19b
  selection passes CTest 13/13; `chrononmotion_camera_v1` reports 77 checks,
  0 failures.
- ChrononTemplate `release-fast`: shared camera baker, lowering, camera packs,
  UI choreography and CM3D contract build 188/188; focused CTest passes 7/7.
- T05 audit found only the Bullet/Charts external-submit harness under
  `e2e_cert`; it has no camera payload or local render result, so the camera
  gate is `NOT RUN`.
- These are local dirty-worktree results. The full C3D preset build, same-SHA
  GitHub CI, and end-to-end shutter-pixel parity remain open.

### Focused shutter-pixel gate closed (2026-10-22, reconciled 2026-10-23)

The focused frame-75 shutter pixel gate now passes: decoded strict-native
Vulkan/NVENC with 90° shutter and eight samples versus the matched software
shutter reference is RGB MAE 1.457, p99 absolute error 5 (declared gate
MAE ≤ 2, p99 ≤ 5). Evidence:
[`shutter-parity-20261022`](../evidence/camera-authority-migration/T16/shutter-parity-20261022/).
CAM-04's same-plan five-frame comparison also passes (MAE 1.429–1.592,
p99 20–21; [`cam04-shared-plan-20261022`](../evidence/camera-authority-migration/T16/cam04-shared-plan-20261022/)).
These close the focused center-frame shutter gate and CAM-04's same-plan
sampled parity only: all-frame and moving-object shutter certification,
non-linear easing error and full exposure accumulation remain open, as does
the pre-encode raw-sink readback defect recorded in the shutter-parity
evidence. The "end-to-end shutter-pixel parity" wording above refers to those
remaining scopes, not to the focused gate.

## Workspace closeout progress (2026-10-21)

Partial implementation of the shared camera-sample contract remains open:

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

The producer inventory and local generated-plan sweep have since closed; see
the 2026-10-21 producer and SDK closeout below. Remaining work is contract and
render certification: offline/runtime sub-frame parity, complete authored
sensor and shutter fields, same-SHA CI, optical gates, and the integrated
T16–T18 cases.

## Verification update (2026-10-21)

The root checkout now points at Chronon3D `a204d0d`, ChrononMotion3D `a460d9d`,
and ChrononTemplate `3872349`. ChrononTemplate's local CMake wiring was updated
so camera emitters link through CM3D's Chronon3D RenderPlan adapter, which
supplies the resolved-sample baker and required include paths. Build evidence:

- ChrononTemplate `cmake --build build/dev-fast -j16`: PASS (130 build steps).
- ChrononTemplate focused CTest selection for camera, plan lowering,
  documentary snapshot, and native primitives: PASS (9/9).
- ChrononMotion3D `cmake --build build/dev-fast -j16 --target
  chrononmotion_camera_v1`: PASS; focused CTest selection for camera,
  authoring, and bake round-trip: PASS (4/4).
- Chronon3D `cmake --build build/chronon/linux-video-test -j16 --target
  chronon3d_camera_tests`: PASS; executable: PASS (73 cases, 65,799
  assertions).

This verifies the current camera libraries and adapter targets; it does not
close the workspace migration. Follow-up edits migrate the 1950 story, Beast
Land reframe, and synthetic cannabis emitters to CM3D's `CameraRig` baker and
serialize `camera_samples`; their focused targets compile with `-j16`. The
1950 emitter was run in no-render mode and produced 210 resolved samples with
no `camera_animation` field; the C3D CLI validated that plan (`valid: true`).
The synthetic cannabis emitter produced 240 resolved samples and no retired
field, but its C3D validation is blocked by its existing missing
`assets/fonts/PlayfairDisplay.ttf` asset reference. The short-phrase emitter
now outputs its perspective-marquee camera through `CameraRig`; generating all
27 plans produced no `camera_animation`, and C3D temporal inspection resolved
the camera across all 210 frames. The single-image emitter now emits resolved
camera samples for its six camera-move plans; its dolly-settle plan passed C3D
inspection. The dark-NBA article emitter also now uses the CM3D baker and its
focused target builds; end-to-end emission could not run because `hb-shape` is
unavailable in the environment. A source audit still finds RenderPlan
producers with the retired field in
`tools/web_presets/build_nba_dirk_physical_fold.mjs`,
`tools/nba_rivalry/build_precomp_2.mjs`,
`tools/nba_rivalry/build_timeline_10.mjs`,
`tools/important_phrases/build_text_depth_focus_v1.py`,
`tools/important_phrases/render_editorial_didone_canary.py`, and
`tools/entities_with_text/emit_cinematic_documentary_narrative.cpp`. The
`camera_animation` payload in `tools/map/render_dynamic_map_overlay.py` is a
separate map choreography selector, not a serialized RenderPlan camera track.
The camera key name in `tools/map/render_map_editorial_pack_v1.py` selects
that same internal map choreography. `tools/camera_roll/dump_scene_camera_poses.cpp`
contains only a stale comment. No checked-in Template JSON plan currently
contains the retired RenderPlan field.

This earlier inventory is superseded by the producer and SDK closeout below.
The overall ticket remains `ACTIVE` for same-SHA CI and the end-to-end optical,
shutter, and random-access image gates.

## Producer and downstream census update (2026-10-21)

The six previously listed Template producers now pass their emitted camera
channels through `chronontemplate_camera_plan_bake`, which samples scalar
channels with CM3D `Track<float>`, builds a `CameraRig`, and writes resolved
`camera_samples`. The adapter's Z reflection and reflected Euler convention
are applied before lowering. The emitted plans contain no `camera_animation`:

- `tools/important_phrases/build_text_depth_focus_v1.py`
- `tools/important_phrases/render_editorial_didone_canary.py` (the pinned
  Bodoni 72 face was absent; the canary now uses bundled Bodoni Moda Italic)
- `tools/entities_with_text/emit_cinematic_documentary_narrative.cpp`
- `tools/nba_rivalry/build_precomp_2.mjs`
- `tools/nba_rivalry/build_timeline_10.mjs`
- `tools/web_presets/build_nba_dirk_physical_fold.mjs`

The Go workspace sibling `RenderingGen/renderinggen/internal/overlay` is an
additional supported downstream plan producer. Its semantic camera channels
are sampled by the canonical CM3D `CameraRig` baker into complete
absolute-frame `camera_samples`; the transient `CameraAnimation` compiler
field is excluded from JSON. The worker requires the packaged baker and fails
closed if it is absent. Focused Go tests cover sample count, interpolation,
zoom, and the wire field. A focused source census found no RenderPlan
`camera_animation`/`camera_samples` consumer in `refactored`; its
`map_camera_animation` fields are semantic map directives, not RenderPlan
camera tracks.

The Template `dev-fast` tree builds with `cmake --build build/dev-fast -j16`.
C3D validates the generated animated text-focus plans, the Didone transition,
and NBA/Dirk plans. The cinematic documentary emitter produces 390 resolved
samples, but full C3D validation is blocked by its pre-existing missing
`assets/backgrounds/cinematic_dark_slate.png` and related image references. A
Vulkan video render of the NBA precomp on the NVIDIA RTX A4000 was decoded
and visually inspected: the corkboard background and 3D player cards are
visible with the camera move. The first conversion attempt exposed an
axis-sign error; it was corrected before accepting this frame. The reviewed
PNG is preserved at
[`output/camera-authority-migration/nba-precomp-vulkan-rtx-a4000.png`](../evidence/camera-authority-migration/nba-precomp-vulkan-rtx-a4000.png). This is a spot check, not the complete image-parity, shutter, or random-access
acceptance suite above. RenderingGen's sampler is now integrated with the
canonical CM3D baker; ticket status stays `ACTIVE` for the remaining gates.

Additional verification from the resumed closeout:

- `go test ./...` in `RenderingGen/renderinggen`: PASS. The new `camera_samples`
  unit test also passes in the full suite.
- ChrononTemplate focused CTest selection: PASS (9/9).
- ChrononMotion3D camera/authoring/bake/parity CTest selection: PASS (11/11).
- Chronon3D `chronon3d_camera_tests`: PASS (73 cases, 65,799 assertions).
- C3D CLI `validate`: PASS for all three animated text-focus plans, the Didone
  camera transition, both NBA rivalry plans, and the Dirk fold plan.
- Vulkan video spot check: PASS after decoding and viewing the rendered NBA
  precomp frame; both the background and 3D player images are present. A
  corrected T16 150-frame Vulkan preview at 854×480 now passes visual review;
  `ffprobe` confirms 150 frames at 30 fps. The five-frame contact sheet and
  remaining limits are recorded in `TICKET-CINEMATIC-COMPOSITION-CERTIFICATION.md`.

The RenderPlan SDK source census finds no serialized `camera_animation` field
in RenderingGen: `CameraAnimation` is compiler-only (`json:"-"`) and its
sampled values are serialized as `camera_samples`. `refactored` has no
RenderPlan camera sample consumer; its similarly named map fields are semantic
map selectors. The remaining blockers are missing assets preventing full
cinematic documentary plan validation and the complete
optical/shutter/random-access image parity gates listed above.

## Camera producer and SDK closeout (2026-10-21)

- All six remaining Template generators in the prior census now route their
  generated camera tracks through `chronontemplate_camera_plan_bake`, built
  from the shared CM3D `CameraRig` baker. The emitted plans contain resolved
  `camera_samples`; the serialized legacy field is removed by the baker.
- RenderingGen's production worker invokes the same CM3D baker executable.
  `CHRONON_CAMERA_PLAN_BAKER` points to the binary packaged by the worker image;
  the production path fails if it is unset or errors. The pure Go sampler is
  retained only as a unit-test reference. The worker's source checkout pins
  CM3D to `a528e1bd83782e67426e4de3b759ef4a18094ad3` (origin/main at audit).
- The workspace SDK census covered Chronon3D, ChrononMotion3D,
  ChrononTemplate, RenderingGen, and `refactored`, plus the public-header and
  SDK install manifests. It found no remaining serialized RenderPlan
  `camera_animation` producer/consumer outside the listed transient Template
  generator inputs (which are baked before output). `refactored` has no
  RenderPlan camera consumer. Its `map_camera_animation` values are semantic
  map selectors. This is a census of the supported SDK/source trees available
  in this workspace; it makes no claim about unprovided third-party forks.
- The local Template `out/` tree was re-baked: 79 existing RenderPlans no
  longer carry `camera_animation`. A fresh inventory of 132 RenderPlan
  outputs found 96 with resolved samples and zero with legacy fields. C3D
  CLI validation passed for all 96 sample-bearing plans using their workspace
  or generated asset roots.
- Build verification: Chronon3D CLI and shared baker build with `-j16`;
  ChrononTemplate's shared baker target builds with `-j16`; the RenderingGen
  full Go suite passed with the actual C3D/CM3D baker configured.
- Visual verification on this host used the NVIDIA RTX A4000. The decoded
  Vulkan NBA precomp frame shows the expected corkboard and 3D player cards
  during camera motion; evidence is preserved at
  `docs/evidence/camera-authority-migration/nba-precomp-vulkan-rtx-a4000.png`.
  The separate T16 150-frame, 854x480 preview also passed the documented
  visual spot check. These are visual spot checks, not completion of the
  ticket's full parity, optical, shutter, or random-access certification.

The producer migration, worker authority integration, and available-workspace
SDK census are closed. Ticket status remains `ACTIVE` only for the broader
rendering certification gates and the documentary canary's missing source
assets; those are tracked limitations, not unfinished producer migration.


### T16 optical probe update (2026-10-21)

CAM-01's 30°/50° FOV ablation now passes using the raw pixel-truth sink at
frame 75 on software and Vulkan. MAE between FOV variants is 62.38 and 62.37
RGB respectively, with 94.79% of pixels changing. At fixed FOV, cross-backend
MAE is 0.081 (30°) and 0.131 (50°). The still-image lane was initially used
by mistake; it omits composed text and per-frame/backend variation, so its
zero-difference result was invalid and is retained only as a labeled control.
Evidence: `Chronon3d/docs/evidence/camera-authority-migration/T16/fov-ablation/`.
This closes only CAM-01; the remaining end-to-end gates and T16–T18 cases keep
the migration `ACTIVE`.

### T16 dolly perspective probe (2026-10-21)

CAM-02 now passes a focused raw Vulkan pixel-truth probe. With the title at
depth 0, moving camera Z from -1000 to -1500 changes its alpha-bbox dimensions
by ratios 0.6674×0.6659, matching the 0.6667 perspective prediction. A centered
subject plane at depth -99 changes bbox width by 0.6436, matching the 0.6431
prediction. See `Chronon3d/docs/evidence/camera-authority-migration/T16/dolly-isolated/`.
The full T16 sequence remains partial; this closes only the isolated dolly
scale gate.

T16 CAM-05 DoF ablation (2026-10-21) fails on Vulkan: enabling DoF on all
camera samples of the three-depth plan changes frame 75 from visible RGBA to
exactly zero RGBA; disabling only the DoF enabled flag restores the frame.
The projected image-plane path supplies CPU depth, while
`VulkanBackend::apply_per_pixel_dof` requires a native depth surface. The
default Vulkan raw job still exits successfully, so the output is a silent
zero-frame failure. Evidence, plan, raw output, timing sidecar, and log are in
`Chronon3d/docs/evidence/camera-authority-migration/T16/dof-ablation/`. CAM-05
remained open after this raw-readback experiment.

### CAM-05 strict native GPU retest (2026-10-21)

The raw-sink failure above is specific to the default readback route, which
supplies CPU depth to the Vulkan backend. The same DoF-enabled plan now renders
successfully through `require_gpu_native_full_graph` and native NVENC for 150
frames. The sidecar records zero GPU readback bytes, zero encoder staging-copy
bytes, and zero execution downgrades. Reviewed A/B samples at frames
0/30/75/120/149 show a 19.95% mean reduction in face ROI edge gradient while
the title ROI remains at 98.27% of its DoF-off sharpness. The focused CAM-05
gate now passes; see `Chronon3d/docs/evidence/camera-authority-migration/T16/dof-ablation/`.
The raw Vulkan readback limitation remains documented separately.

## External engine reuse decision (2026-10-22)

No external engine component is adopted as a production dependency in this
camera closeout. CM3D remains the single owner of camera authoring, lens/focus
decisions, spatial motion and sampling; C3D remains the owner of projection,
pixel depth, DoF, motion blur and compositing. Template continues to consume
resolved CM3D samples through the shared baker.

The first external comparison to run, if DoF quality/performance remains a
measured gap, is AMD FidelityFX DoF against C3D's existing `PerPixelDofNode`.
Any accepted implementation must replace or improve that node's kernel and
stay inside the existing C3D RenderGraph; it must not add another DoF owner.
The official FidelityFX SDK currently documents DoF 1.1, Vulkan + GLSL 4.50,
and MIT licensing; this establishes a plausible API/license fit, not verified
build, shader, or image compatibility with Chronon3D.
Filament's camera API is a reference for checking the shared focal-length/FOV,
sensor and focus contract, not a second camera model. No FidelityFX or Filament
code has been copied or linked. Before integration, record the exact source
revision, per-file license and dependency notices, Vulkan compatibility,
quality/performance comparison, and pixel/alpha equivalence results. See the
[FidelityFX DoF overview](https://gpuopen.com/fidelityfx-dof/) and its
[Vulkan technique requirements](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/depth-of-field/).

### EffectCraft comparison (2026-10-22)

EffectCraft is a useful architecture reference for the shutter issue. Its
documented Advanced 3D path prepares each motion-blur sub-sample, rasterizes
those samples on wgpu, averages them on the GPU (retaining nearest depth), then
applies depth-based DoF and composites without a frame readback; its CPU renderer
is used as the comparison oracle. EffectCraft is dual-licensed MIT/Apache-2.0,
but it is a Rust/wgpu engine, so no source or dependency is being transplanted.

Chronon keeps its ownership contract: CM3D supplies exact shutter-time camera
samples; the existing C3D temporal compositor renders and accumulates them. The
local failure exposed that nested graph scopes were trying to open independent
Vulkan frame batches; C3D now supports nested begin/end scopes and has a ring
regression test. The strict-native NVENC shutter path is pixel-checked at T16
frame 75. The no-encode raw sink still fails to produce a valid result and stays
open; EffectCraft's design reinforces the next requirement: keep the accumulator
device-resident and synchronize only at the requested terminal consumer.

References: [EffectCraft architecture](https://github.com/storytold/effectcraft/blob/main/docs/architecture.md),
[GPU/CPU parity measurements](https://github.com/storytold/effectcraft/blob/main/docs/target-app-parity.md),
[project license](https://github.com/storytold/effectcraft#license-and-credits).

### Local adapter closeout build (2026-10-26)

- Repaired the split ChrononMotion3D RenderPlan adapter sources: shared lowering
  helpers are included by the main lowering unit, geometry uses the canonical
  RenderPlan path-command type, and layer validation has balanced scope.
- `chrononmotion_chronon3d_adapter` builds and links in the video-enabled C3D
  configuration with `-j16`. `chronon3d_motion_plan_adapter_tests` passes
  10/10 cases and 159 assertions. The camera test now checks the documented
  compatibility mapping for the legacy `dof.aperture` wire field
  (`blurGainFromFStop(2.8)`) while f/stop remains the authored optical value.
- `linux-fast-dev` builds `chronon3d_dev_fast` with `-j16`; camera tests pass
  73/73 cases and 65,799 assertions. Daemon concurrency passes 25/25 cases and
  163 assertions, and node-memory telemetry passes its focused case (11
  assertions).
- This closes local adapter build and value-transport checks only. At this
  checkpoint the GPU was occupied, so no new camera or VRAM render was
  launched. See the 2026-10-27 closeout below for the subsequent all-frame
  shutter render; full optical and integrated T16–T18 certification remains
  open.

### T16 all-frame shutter parity closeout (2026-10-27)

The T16 three-depth RenderPlan was rendered for all 150 frames at 1920×1080
with a 90° shutter and eight samples. The raw Vulkan render and matched software
reference both contain 150 complete RGBA8 frames (1,244,160,000 bytes). Every
frame passes the existing MAE ≤2 / p99 ≤5 pixel limits: mean per-frame MAE
0.10996, maximum 0.11955, maximum p99 2. The all-frame T16 shutter parity gate
is closed before encoding; this extends the prior selected-frame result.

The same plan completes a full strict-native Vulkan/NVENC output with 150
frames and packets, zero downgrades, and zero GPU readback, staging-copy, or GPU
surface-copy frames. The lifecycle counter reaches 11,128,848,384 VMA surface
bytes at frame 150 on the 16 GB RTX A4000. Decoded compressed output is recorded
separately: 111/150 frames meet MAE ≤2 / p99 ≤5; maximum p99 is 8. Thus raw
renderer parity passes across the full T16 shutter sequence, while decoded
compressed-output parity at the same strict p99 limit remains open.

Metrics and per-frame measurements are in
[`T16 full-sequence shutter evidence`](../evidence/camera-authority-migration/T16/shutter-parity-20261022/t16-shutter-full150-raw-metrics-20261027.json)
and the [decoded NVENC comparison](../evidence/camera-authority-migration/T16/shutter-parity-20261022/t16-shutter-full150-metrics-20261027.json).
The plan, 150-frame NVENC video and sidecar, and timing metadata are retained in
that evidence directory. Both 1.24 GB raw sequences (Vulkan and software
reference) are under the ignored build tree.
