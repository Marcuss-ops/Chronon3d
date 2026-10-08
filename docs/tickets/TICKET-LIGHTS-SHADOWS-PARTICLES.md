# TICKET-LIGHTS-SHADOWS-PARTICLES — RenderPlan lighting and CPU particle batching

## Goal

Close the RenderPlan path for authored lights, projected shadows and particle
layers. Vulkan instancing is explicitly out of scope because this checkout has
no Vulkan backend. The current RenderPlan light is a radial screen overlay,
and particle layers expand into one geometry node per sampled particle.

## Acceptance criteria

- [x] RenderPlan light payload accepts `point`, `spot`, and `directional`, with
  validated position/direction, range, decay, color and intensity.
- [x] Lights affect layers below them in stack order. Point, spot and
  directional CPU lowering and point/spot attenuation are covered by tests.
- [x] A light can cast a projected silhouette shadow with validated blur and
  bias. Caster/receiver opt-outs are honored and tested.
- [x] Light position, intensity and color can be animated by RenderPlan tracks;
  the field names and sampling semantics match ChrononMotion3D light tracks.
- [x] CPU particle rendering batches opaque normal-blend particles of matching
  paint while retaining authored order. The batched-path structure is covered
  by tests. Other blend modes and sprites retain individual draws pending a
  typed particle-batch renderer.
- [ ] Record meaningful CPU timings for 1k, 10k and 100k particles, and verify
  rendered pixel output. The current CLI PNG lane explicitly warns that it is
  not pixel-truth; `--video-sink raw` rejects RenderPlans requiring the legacy
  compiled export view, so an end-to-end pixel check is blocked by the CLI.
- [x] Build affected targets and run targeted tests. The full C ABI suite runs
  261 cases but currently has one unrelated failure in temporal-rate analysis.
- [x] Vulkan instanced drawing and CPU/GPU parity are deferred because this
  checkout has no Vulkan backend.

## Audit

- Chronon3d already has deterministic CPU particle sampling, but expands samples
  to individual circle/image nodes in `render_plan_compiler_scene.cpp`.
- RenderPlan light layers currently compile to a radial circle and default to
  screen blending. They do not contribute illumination to lower layers.
- 3D scene rendering has a `LightContext`, material light reception and shadow
  settings, but those are not wired as per-light RenderPlan layers.
- RenderPlan layer `casts_shadows` and `accepts_shadows` are currently erased
  during normalization; 2D projection from a RenderPlan light is not present.
- ChrononMotion3D exposes light position, intensity and color tracks in
  `chrononmotion/motion/Lighting.hpp` and writes the `position`, `intensity`,
  and `color` track vocabulary from `SceneExport.cpp`.

## Implementation record

- Light kinds/range/decay/spot cone/shadow fields live in the RenderPlan v2/v3
  schemas and `LightLayerPlan`; decoder validation and fingerprints include
  every field. `light_intensity` and `light_color` animation use the same
  property and track vocabulary as ChrononMotion3D; position uses the existing
  layer position tracks.
- CPU point and spot lights lower to screen-space light contributions with
  range and decay falloff; directional lights lower to a full-canvas
  contribution. Light layers are composited in authored order.
- `casts_shadow` lowers to the existing drop-shadow effect on prior eligible
  layers. The projection offset derives from light-to-caster direction and
  depth; blur and bias are read from the light payload.
- CPU particle batching is limited to opaque normal-blend particles and batches
  contiguous equal-color runs into one non-zero-winding path each. Translucent,
  additive and sprite particles remain ordered individual nodes for compositing
  correctness. The C ABI suite (`ctest --test-dir
  .tmp/chronon-builds/linux-fast-dev --output-on-failure -R
  '^chronon3d_c_abi_tests$'`) reports 260/261 passing; the failure is the
  existing `Temporal rate analysis classifies all five required rates` check
  (`{3, 7}` versus `{3, 4, 5, 6}`), also reproducible in isolation. Targeted
  decoder, validator, batching, light-kind, shadow, animation and registry tests
  pass. `chronon3d_c_abi_tests` and `chronon3d_cli` build successfully. JSON
  schema parsing and `git diff --check` pass. CLI render invocation succeeds,
  but its PNG output cannot serve as visual evidence due to the documented
  non-pixel-truth lane; raw output rejects this RenderPlan route. CPU timings
  and verified combined-scene pixels remain open.

## Demolition Debt

- **Owner:** Chronon3d RenderPlan maintainers.
- **Reason:** Existing radial-light preview and per-particle node expansion
  must remain available only as compatibility during the new lowering path.
- **Exit condition:** New light contribution and batched CPU particle path pass
  parity/contract gates and all repository consumers use them.
- **Equivalence test/gate:** RenderPlan light/particle golden and deterministic
  frame-hash comparisons, plus CLI combined-scene fixture.
- **Removal scope:** Remove radial light overlay lowering and per-particle node
  expansion from `render_plan_compiler_scene.cpp`.
- **Status:** ACTIVE.
