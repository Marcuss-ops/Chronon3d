# TICKET-CAMERA-LENS-SHIFT-EDITORIAL-LOOKS — closeout record

## Scope

Track the camera, card, effects, material and per-glyph gaps raised in the
2026-10-08 Chronon3d/ChrononTemplate audit. This ticket records what was
already implemented, what this change closes, and what still needs a dedicated
rendering design.

## Acceptance and status

- [x] A viewport-relative principal point/lens shift is consumed by point,
  layer and matrix projection paths; Camera V1 copies it from the base spec and
  includes it in the camera program fingerprint.
- [x] ChrononTemplate has direct static FOV and horizon-roll controls. Existing
  animated FOV tracks and FOV-aware camera packs were present before this work.
- [x] ChrononTemplate image styles can request warm light rays/glow, radial RGB
  separation, temporal echo, and alternating mesh-warp bands through existing
  RenderPlan CPU effects. These are authored approximations, not an optical
  light-leak shader or channel-isolated trail simulation.
- [x] RenderPlan inline mesh layers no longer have a separate effect whitelist;
  validated layer effects follow the same post-effect path as other sources.
- [x] Per-glyph 3D transform state already exists in `GlyphInstanceState` and
  the text-run matrix/raster path; no duplicate element-state model was added.
- [x] Wire `Card3DMaterial` into normal projected-layer rendering. Configured
  projected layers now overlay the software-rendered thickness, edge highlight
  and rim light; the front image remains intact. Card shadow switches feed the
  existing layer lighting/shadow path. Native strict rendering does not support
  this overlay yet and reports unsupported execution rather than silently
  dropping it.
- [x] Import glTF metallic/roughness factors and the metallic-roughness image
  reference into immutable `MeshMaterial`; validate factor ranges, image and
  texture indices, and the supported UV set.
- [x] Propagate `CHRONON3D_ENABLE_MESH` and meshoptimizer through the asset
  target, and expose the feature define to the core tests. Previously the CMake
  option selected the dependency without compiling the gated loader/test code.
- [x] Decode and sample the imported roughness map per pixel in software mesh
  lighting (roughness=G, metallic=B), with perspective-correct UV
  interpolation and an embedded-image rendering test.
- [x] Feed prepared PBR material values into the production mesh lighting path
  when the scene lighting context is enabled; the unlit legacy path remains
  available when lighting is disabled.

## Demolition debt

- **Owner:** Chronon3d rendering maintainers.
- **Reason:** Template-level looks intentionally compose existing CPU effects
  while dedicated optical/glitch operators are not available.
- **Exit condition:** dedicated effects provide pixel-truth parity and replace
  the `ImageFrameStyle` compositions where appropriate.
- **Equivalence test/gate:** deterministic RenderPlan image/frame comparison
  against the current compositions.
- **Removal scope:** `ImageFrameStyle` look fields and the effect composition in
  `ChrononTemplate/src/chronontemplate/PlanLowering.cpp` if native operators
  replace them.
- **Status:** ACTIVE.

## Follow-up implementation slice

The loader decodes supported embedded PNG/JPEG metallic-roughness images into
bounded RGBA8 storage. The software mesh rasterizer interpolates UVs
perspective-correctly, samples G/B channels, and applies the imported factors
to per-pixel lighting.

Focused checks for this slice passed: loader fixture (26 assertions),
bounds/range fixture (20 assertions), roughness-map render (1 case), card
rasterizer overlay (1 case, 2 assertions), camera projection suite (13 cases),
and inline mesh C ABI suite (6 cases). The inline-mesh post-effect decoder test
passes (5 assertions); the scene-builder card API test passes (5 assertions).
Per-glyph initialization, position, scale, from-center rotation, and opacity
tests pass; the TextRunDriver suite passes all 19 cases (78 assertions). The
mesh-disabled full core run completed with 1,478 passing and 8 failing cases,
including a static-mesh contract expectation evaluated without the mesh
feature and font-dependent fixture failures. The full render-graph suite has one
deterministic golden hash mismatch (`0x1ec334584fada888` versus the checked-in
`0x65304b41dbf40d8f`); its other 420 cases passed.
