# Color Matrix effect

## TODO — cosa manca (agg. 2026-10-27)

- [ ] Eseguire i test (build ok ma test NON eseguiti in questa pass): schema, effetti, parità software/Vulkan del contratto premoltiplicato.

## Scope

Add a canonical 4x5 RGBA color matrix to Chronon3d so authoring can express
duotone, sepia, channel remapping, alpha transforms, and other affine grades
without a dedicated effect per look.

## Implementation

- Canonical ID: `color.matrix` (effect type 53).
- Coefficients are row-major: four output rows, each containing RGBA weights
  followed by a bias. Coefficients are bounded to `[-16, 16]`.
- Render Plan catalog parameters use `m00`...`m33` and `b0`...`b3`; the
  coefficients are animatable.
- Software execution uses the existing fused `ColorPipeline`.
- Vulkan reuses the native color-matrix compute kernel and preserves the
  software premultiplied-alpha contract; output alpha is clamped to `[0, 1]`.
- No Skia dependency is introduced.

## Validation

The shader compiled, passed SPIR-V validation/optimization, and the affected
Chronon3d C++ targets built successfully. Tests were not run in this pass.
