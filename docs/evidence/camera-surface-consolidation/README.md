# Camera surface consolidation — consumer census (2026-10-31)

Evidence for `TICKET-CAMERA-SURFACE-CONSOLIDATION` steps **S0**, **S1**, **S3**,
**S4**. S3 passes three pinned raw software sentinels. Production 2.5D
image/text/shape producers now emit resolved camera/layer samples. F3 is PASS
on the repeatable two-card PNG crossing canary; S2 can proceed. Documentary
Showcase now resolves its animated Particle layer too; its Adjustment layer is
static and has no authored Motion transform track. The video compatibility
view remains an active, separate demolition debt.

## Method

Direct-include census over the working trees (`26f61715a` plus this pass):

```shell
# C3D
grep -rl --include='*.hpp' --include='*.cpp' --include='*.cc' \
  --include='*.c' --include='*.h' '<header path>' include src tests apps
# CM3D
grep -rl --include='*.hpp' --include='*.cpp' '<header path>' \
  include src tests tools
```

Counts are **include sites**, not transitive consumers; umbrella includes are not
expanded. The table records the 2026-10-31 census before the cleanup below.

## 1. C3D — classification and consumers

| File | Bucket | Includes | Representative consumers |
|---|---|---:|---|
| `math/camera_projection_contract.hpp` | Admission contract | 11 | `detail/camera_projection_{frustum,matrix}`, resolver, `camera_2_5d_projection`, `evaluated_projection` |
| `math/camera_projection_resolver.hpp` | Projection authority | 6 | `camera_2_5d_projection`, four math tests |
| `math/detail/camera_projection_matrix.hpp` | Internal detail | 1 | resolver |
| `math/detail/camera_projection_clip.hpp` | Internal detail | 2 | resolver, `test_clip_with_uv` |
| `math/detail/camera_projection_frustum.hpp` | Internal detail | 2 | resolver, `test_frustum_culling` |
| `math/camera_2_5d_projection.hpp` | 2.5D projection | 24 | `projection_helpers`, `projector_2_5d`, `projected_card_rasterizer`, `nulls_overlay`, `preflight`, `source_node_native_image` |
| `math/camera_pose.hpp` | Pose type | 5 | camera model and projection headers |
| `math/projection_context.hpp` | Projection context | 4 | `render_graph_context`, `render_graph_node`, `projector_2_5d`, `transform` |
| `scene/model/camera/camera_2_5d.hpp` | Camera model | 49 | `catmull_rom_path`, `render_settings`, `post_processing_system`, `render_graph_context`, `per_pixel_dof_node` |
| `scene/model/camera/dof.hpp` | DoF model | 17 | `per_pixel_dof`, `post_processing_system`, `render_backend_frame_api`, `per_pixel_dof_node`, `render_plan_camera_adapter` |
| `scene/model/camera/camera.hpp` | Scene camera | 13 | scene builder and adapters |
| `scene/model/camera/lens_model.hpp` | Optics model | 3 | camera model and projection source |
| `scene/camera/camera_projection.hpp` | Projection utility | 3 | projection tests and stabilization test |
| `scene/camera/camera_state_builder.hpp` | Renderer-state value helper | 1 direct include | `scene_builder` umbrella; 19 call sites across camera, DoF, media, mesh, lighting, graph, and camera-path tests |
| `render_plan/camera_contract.hpp` | Admission contract | 4 | decoder, compiler validation, compiler, camera tests |
| `render_plan/camera_sample_provider.hpp` | Provider contract | 2 | render-plan compiler, compiler |
| `render_graph/nodes/per_pixel_dof_node.hpp` | DoF graph node | 3 | node implementation, output pass, unit test |

**S4 disposition.** `camera_projection_{matrix,clip,frustum}` are now
`math/detail/` and remain package-closure entries, not SDK API. The resolver and
`camera_contract.hpp` / `camera_projection_contract.hpp` stay the pinned
admission points. The diagnostic overlay, shot report, and evaluated
projection snapshot were removed in the current cleanup after their callers
were removed; the golden projection test now exercises the canonical
projection contract directly. `CameraStateBuilder` was retained after the full
call-site audit found 19 test clients. It only constructs a renderer-ready
value and does not evaluate authoring, motion, or constraints.

## 2. CM3D — camera headers

| Header | Bucket | Includes | Representative consumers |
|---|---|---:|---|
| `CameraRig.hpp` | Authority | 22 | `DocumentaryCameraPath`, `CameraSequence`, `CameraPanelChoreography`, `AspectReframe`, `TitleCameraRecipe` |
| `CameraMotion.hpp` | Applier | 12 | `AspectReframe`, `Motion.hpp`, `EyeTrace`, `Presets`, `GeoCameraRig` |
| `CameraFocusSequence.hpp` | Applier | 3 | `DocumentaryCameraPath.cpp`, implementation, focus-sequence test |
| `CameraRecipe.hpp` | Applier | 4 | `DocumentaryCameraPath.cpp`, recipe registry, implementation, camera test |
| `CameraSequence.hpp` | Applier | 2 | implementation, sequence test |
| `CameraTrajectory.hpp` | Applier | 3 | `Motion.hpp`, implementation, trajectory test |
| `CameraPanelChoreography.hpp` | Applier | 3 | `Motion.hpp`, implementation, framing test |
| `DocumentaryCameraPath.hpp` | Applier | 2 | implementation, documentary test |
| `TitleCameraRecipe.hpp` | Applier | 2 | implementation, framing test |
| `CameraScreenSpace.hpp` | Geometric service | 11 | `AspectReframe`, `Interaction`, `Motion.hpp`, `EyeTrace`, `CameraValidation` |
| `CameraFraming.hpp` | Geometric service | 5 | framing, documentary, sequence, recipe, framing test |
| `CameraValidation.hpp` | Validation service | 3 | `Motion.hpp`, implementation, framing test |
| `CameraShake.hpp` | Procedural service | 2 | implementation, shake test |

**S2 status — shared track writer adopted and moved out of the public rig header, 2026-10-31; header/data collapse still open.**
`CameraRigTrackBatch` is the sole track-write path used by camera appliers in
`chrononmotion/motion/`: camera motion recipes, trajectory, transition,
focus/sequence/recipe/panel/title/documentary appliers, and camera presets.
The implementation now lives in the source-private
`src/chrononmotion/motion/CameraRigTrackBatch.hpp`; `CameraRig.hpp` no longer
publishes the queue/proxy mechanism as part of the rig API.
The writer preserves `Track::add` ordering and flushes sampled paths in bounded
chunks. Its unit canary compares sampled output with direct `Track::add`,
including a duplicate key inserted after a flush. Eight targeted CM3D
authoring/camera tests passed before this move; the current post-move build and
7 focused authoring/camera tests pass. A source audit finds zero direct
`rig.*Track().add` sites in the Motion camera/preset sources.
The existing camera bake roundtrip and RenderPlan camera parity gates also
pass after routing the appliers through the writer.

This consolidates mutation but does not yet satisfy the ticket's full target of
typed declarative preset data and a smaller public header set. The camera
helpers encode different behaviors:
continuous spline sampling (`CameraTrajectory`), editorial handoffs and cuts
(`CameraSequence`), focus-only tracks, shakes, title/documentary recipes, and
screen-space reframing. Removing headers or wrapping them in an umbrella would
only hide the surface, not remove duplicate authority or implementation. The
remaining work is to convert composable editorial appliers to typed data,
compare sampled poses and lowered plans against the current authoring outputs,
then remove only headers whose semantics and consumers have moved. F3
base/animation samples and the rendered depth-order canary pass
(`camera-f3-depth-order.json`).

## 3. S3 — legacy `dof.aperture` wire field migration

The coordinated source migration is present in this working tree:

- CM3D lowering and emission no longer serialize `camera.dof.aperture`; the
  authored wire unit is `camera.dof.f_stop`.
- C3D removed `CameraPlan::dof_aperture`, its fingerprint input, and the schema
  property. Decoder tests require old wire input to fail closed.
- `CameraPlan` now has one typed `f_stop` and one typed `iris_blades`; the
  accepted nested wire spellings normalize there and conflicting values fail
  closed. Fingerprints and adapters consume the canonical fields only.
- `render_plan_camera_adapter.hpp` derives the renderer's internal
  `DepthOfFieldSettings::aperture` from f-stop. That runtime kernel parameter is
  still required by the current DoF implementation; it is no longer a second
  RenderPlan field or an authoring unit.
- The source cleanup removed C3D's camera debug overlay family, shot report,
  and evaluated projection snapshot. `CameraStateBuilder` remains because its
  19 callers need the renderer-ready value helper. `camera_plan_bake` remains
  because the Docker/SDK packaging references it; retiring it requires migrating
  that process-boundary consumer to resolved samples.

**Verifica 2026-10-31:** build Motion `chrononmotion_authoring` PASS e test
camera/projection/bake 11/11 PASS; `chronon3d_camera_tests` PASS. Frame DoF raw
software 30/75/149 del piano canonico corrente sono byte-identici ai baseline
archiviati (8.294.400 byte ciascuno; MAE/p99/max = 0); dettagli e digest in
[`dof-wire-raw-equivalence.json`](dof-wire-raw-equivalence.json). Il gate S3
dei sentinels è quindi **PASS**. Non certifica il video intero, Vulkan, né la
parità AE. Il gate architetturale C3D è PASS **214/214** dopo aver rimosso la
scansione di `content/`, directory non presente nel repository.

Il test `PR2-RG-DoF` include prove software con due card 2.5D sintetiche sovrapposte:
la card vicina opaca occlude quella lontana; con opacity 0,5 entrambe le card
contribuiscono al composito e l'alpha finale resta 1, in entrambi gli ordini di
inserimento. La suite DoF passa 16 casi / 979 asserzioni. La prova alpha è una
fixture locale, non una matrice completa; fault injection di depth native ed
equivalenza Vulkan restano fuori da questo PASS.

## 4. S5 — resolved transform bake migration (partial)

Materialized Template plans and the camera-backed Motion producers Optical
Studio, Word Rack Focus, camera-backed Map Flyover, and Documentary Showcase
request `requireResolvedLayerSamples`. Motion's camera-free Showreel producer
does too: resolved layer samples do not require a camera rig. Presentation
frames (or the canonical shutter calendar) are emitted as V3
`transform_samples` on visual Image, Video, Color, Text, Shape, Particle and
Adjustment layers. Light and Mesh remain excluded because they have different
consumer semantics. The Word Lens fixture emits 361 samples on each of eight
visual layers; Showreel emits 151 on each of eleven text layers. Documentary
Showcase emits 151 samples for each of its 14 animated visual layers; the
Adjustment layer has no Motion transform source or authored transform track,
so it stays static in its declared plan state. The camera parent Null is
omitted from renderable output because its world transform is already resolved
into the camera and child layers. Test details and source digests:
[`resolved-sample-migration.json`](resolved-sample-migration.json).

This closes only the enumerated producer path. The compatibility video view is
still active and is not covered by this transform-sample migration.

The compatibility boundary now has a separate raw software canary in
`tests/renderer/camera/test_camera_motion.cpp`: a two-card 2.5D plan with
camera samples and shutter 180°/4 produces identical `render_program` and
`render_compiled(make_video_compatibility_view(...))` framebuffer hashes for
frames 0, 1, and 2. This proves the sampled camera/layer payload is preserved
by that synthetic bridge case. It does not prove mux/encoder equivalence,
strict-native output, a broad corpus, or removal of the video view; S5 remains
open for those gates.

## 5. Acceptance commands (run on the build host)

The prior pass verified the projection path moves with `git diff --check` and
the include census. The current pass builds C3D with 16 jobs; runtime tests and
Gate C remain separate gates.

```shell
# C3D — public surface and camera/math suites
python3 tools/check_public_headers.py \
  --compile-commands build/chronon/linux-video-release/compile_commands.json \
  --manifest cmake/Chronon3DPublicHeaders.cmake --out out
cmake --build build/chronon/linux-video-test --target chronon3d_camera_tests chronon3d_core_tests -j12
ctest --test-dir build/chronon/linux-video-test -R 'camera|clip|frustum' --output-on-failure

# CM3D — boundary guardrail
ctest --test-dir build/release-fast -R chrononmotion_boundary --output-on-failure
```
