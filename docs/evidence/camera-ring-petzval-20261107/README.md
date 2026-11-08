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

Coverage:

1. Elliptical CoC geometry (`smoothstep` monotonicity, 0.5 iso-contour axis ratio).
2. Single-pixel Nyquist checker: exact center copy, lateral collapse with mean
   horizontal gradient below 0.05, and exact constant-image conservation.
3. Typographic three-zone check: center edge ≈1 px, lateral edge ≈30 px, luminous
   energy error 0.00633 (0.63 %, inside the 1.5 % budget).
4. Spectral isolation: central probe has zero R/B separation; lateral probe >0.05.
5. Dynamic camera-speed coupling: safe zone narrows and blur radius grows, with
   clamping at the 1200 units/s endpoint.
6. CPU determinism and bound: 640×360, 16 taps, **1581 ms**, identical repeat output.

A 2×2 pixel sensor-integration prefilter precedes the Poisson taps in both CPU
and GPU sampling. It removes checker-frequency residue that a 16-tap stochastic
kernel otherwise leaves at the Nyquist limit. The elliptical safe-zone bypass
remains an exact copy.

### Shader ABI

Command:

```bash
./tests/chronon3d_shader_abi_consistency_tests
```

Result: **22/22 test cases, 188/188 assertions passed**. This validates the
generated camera-ring shader/ABI headers (including the 48-byte push constant).

### Canonical RenderPlan lowering

`SceneMaterializer` now lowers `EffectKind::CameraRing` into
`CameraRingParams` instead of dropping it in the effect switch. This is what
makes `render --plan` execute the effect on the compiled-program route.

`Chronon3d/tests/fixtures/camera_ring/camera_ring_petzval_demo.plan.json`
validates as 1920×1080, 300 frames, digest:

```text
64d99a2b9e08b3108654cec9c06c1d147e7832b692dabb9c08b12aaaffefc4d0
```

### 1080p Vulkan execution

Command:

```bash
chronon3d_cli render \
  --plan Chronon3d/tests/fixtures/camera_ring/camera_ring_petzval_demo.plan.json \
  --assets-root "$PWD/Chronon3d" \
  --backend vulkan --gpu-hot-path-mode require_gpu_native \
  --frames 0-299 \
  -o Chronon3d/docs/evidence/camera-ring-petzval-20261107/camera_ring_petzval_1080p.mp4
```

Output: 1920×1080, 30 fps, 300 frames, H.264.

Native execution is confirmed by the timing sidecar:

```json
"blur.camera_ring": {
  "native_frames": 1,
  "admission_rejected_frames": 0,
  "software_dispatches": 0,
  "fallback_count": 0
}
```

The scene is static, so the fast path reuses 299 rendered frames; exactly one
`blur` pass executes. Its GPU timestamped duration is:

- `blur`: **10.329696 ms** (min = mean = max, one execution)
- `blur` pass count: 1
- No software fallback or admission rejection

Decoded `frame_000.png` and `frame_150.png` show lateral glyphs visibly
defocused with radial chromatic separation, while the central “or” remains
sharp.

## Budget result — PASS (2026-11-08 kernel)

**PASS.** The optimized 1080p `blur` pass measures **0.428 ms** on the RTX
A4000, inside the requested **<0.45 ms** budget. The same native pass measured
**10.33 ms** before the kernel change (about 24× slower); that earlier number
is kept in the history above. The 300-frame sidecar contains one blur sample
because the static fast path reuses the effect output for the remaining 299
frames; no p95 across 300 executed passes exists.

The shader now shares one golden-angle gather per 4×4, 8×8 or 16×16 pixel
block when the circle of confusion is large (16×16 uses 8 taps, 8×8 uses 16),
while small radii keep the per-pixel 2×2 prefilter and the full tap count. The
elliptical safe zone is still copied exactly per pixel, and the fallback at a
sharp block origin preserves the boundary. Decoded frame 0 differs from the
previous render by MAE 1.06/255 (p99 36, max 177 at grain/edges); the layout
and the sharp centre are unchanged. This is a diagnostic, not a new visual
acceptance gate.

The still/PNG lane is not a supported Vulkan pixel-truth surface and crashed
(`SIGSEGV`, exit 139) when asked for a still; the video lane above is the
supported and verified path.
