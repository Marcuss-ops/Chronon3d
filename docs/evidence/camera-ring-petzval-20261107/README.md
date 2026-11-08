# Camera Ring / Petzval Field Curvature — diagnostic evidence

Date: 2026-11-08
Effect: `blur.camera_ring` (`EffectType::CameraRing`, catalog value 62)

## Verified

### CPU diagnostics

Command (from `Chronon3d/build/chronon/linux-video-release`):

```bash
./tests/chronon3d_effects_tests --test-case="CameraRing*" --success
```

Result: **6/6 test cases, 779/779 assertions passed**.

The six cases cover:

1. Elliptical CoC geometry (`smoothstep` monotonicity, 0.5 iso-contour axis ratio).
2. Single-pixel Nyquist checker: exact center copy, lateral collapse with mean
   horizontal gradient below 0.05, and exact constant-image conservation.
3. Typographic three-zone check: center edge ≈1 px, lateral edge ≈30 px, luminous
   energy error 0.00633 (0.63 %, inside the 1.5 % budget).
4. Spectral isolation: central probe has zero R/B separation; lateral probe >0.05.
5. Dynamic camera-speed coupling: safe zone narrows and blur radius grows, with
   clamping at the 1200 units/s endpoint.
6. CPU determinism and bound: 640×360, 16 taps, **1581 ms**, identical repeat output.

A 2×2 pixel sensor-integration prefilter was added ahead of the Poisson taps in
both CPU and GPU sampling. It removes checker-frequency residue that a 16-tap
stochastic kernel otherwise leaves at the Nyquist limit. The elliptical safe-zone
bypass remains an exact copy. CPU proxy cost is 1581 ms for 640×360/16 taps.

### Shader ABI

Command:

```bash
./tests/chronon3d_shader_abi_consistency_tests
```

Result: **22/22 test cases, 188/188 assertions passed**. This validates the
generated camera-ring shader/ABI headers (including the 48-byte push constant),
not a live GPU dispatch.

### RenderPlan fixture

`Chronon3d/tests/fixtures/camera_ring/camera_ring_petzval_demo.plan.json`
validates as 1920×1080, 300 frames, digest:

```text
64d99a2b9e08b3108654cec9c06c1d147e7832b692dabb9c08b12aaaffefc4d0
```

## NOT verified: 1080p Vulkan execution and `<0.45 ms`

The CLI `render --plan` path on this checkout does **not** execute layer or
adjustment-layer effects. Observed on the 300-frame 1920×1080 Vulkan render:

- `job.effect_usage` is `{}`.
- `gpu_observed.pass_breakdown_ms` contains `composite`/`layer_batch`/
  `affine_transform` but no `blur` entry.
- Adding the effect to text, image, and adjustment layers all produced the same
  result; the rendered typography stayed sharp.
- Temporary diagnostics in `prepare_layer_effects()` and
  `apply_scene_layer_effects()` emitted no lines, proving the canonical plan
  path never calls those scene-effect preparation functions.

Therefore the 1080p GPU result and the `<0.45 ms` budget remain **unmeasured**.
Wiring `blur.camera_ring` into the canonical plan/program effect lowering is the
required next step; this is upstream of the shader and CPU kernel, both of which
are implemented and tested here.

The still/PNG lane with `--backend vulkan --gpu-hot-path-mode
require_gpu_native` is explicitly unsupported and crashed (`SIGSEGV`, exit 139)
when asked to emit a still; video output is the supported lane.

Temporary probe fixtures/assets and no-effect video frames were removed. The
validated text fixture remains for future canonical-path wiring.
