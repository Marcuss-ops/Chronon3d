# P0-04 camera yaw and inline mesh animation isolation

Local diagnostic evidence recorded 2026-11-03 against the pre-existing
`linux-video-fast-dev` software CLI (binary timestamp 18:22:51 UTC; this was
not rebuilt or certified against a clean commit). The four plans are frozen in
this directory. They use the animated glTF roundtrip fixture's texture assets
from the sibling CM3D checkout:
`../ChrononMotion3d/docs/evidence/ticket-roundtrip-scaled-20261029/gltf`.

Render each plan from the C3D repository root:

```bash
CLI=build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli
ASSETS="$PWD/../ChrononMotion3d/docs/evidence/ticket-roundtrip-scaled-20261029/gltf"
"$CLI" render-plan --input docs/evidence/camera-mesh-yaw-isolation-20261103/moving-mesh-static-camera-zero.plan.json --output /tmp/moving-mesh-static-camera-zero.rgba --assets-root "$ASSETS" --backend software --video-sink raw
"$CLI" render-plan --input docs/evidence/camera-mesh-yaw-isolation-20261103/moving-mesh-moving-camera.plan.json --output /tmp/moving-mesh-moving-camera.rgba --assets-root "$ASSETS" --backend software --video-sink raw
"$CLI" render-plan --input docs/evidence/camera-mesh-yaw-isolation-20261103/static-mesh-moving-camera.plan.json --output /tmp/static-mesh-moving-camera.rgba --assets-root "$ASSETS" --backend software --video-sink raw
"$CLI" render-plan --input docs/evidence/camera-mesh-yaw-isolation-20261103/moving-mesh-static-yaw.plan.json --output /tmp/moving-mesh-static-yaw.rgba --assets-root "$ASSETS" --backend software --video-sink raw
```

For each raw RGBA frame (640×360), count pixels whose alpha byte is non-zero.
Observed counts at frames 0/1/5/15/30:

| Case | Mesh | Camera | Non-transparent pixels |
|---|---|---|---|
| Control | Animated | Fixed yaw 0° | 501 / 452 / 477 / 452 / 477 |
| Reproduction | Animated | Yaw 0°→0.02° | 501 / 350 / 171 / 0 / 0 |
| Camera-only control | Frozen at first mesh pose | Yaw 0°→0.02° | 501 / 402 / 376 / 376 / 375 |
| Mesh-only orientation control | Animated | Fixed yaw 0.02° | 375 / 328 / 171 / 0 / 0 |

The image bounds in the failing case contract toward the viewport center and
then disappear. This rules out camera motion alone as the collapse trigger and
shows the failure can occur with a static non-zero yaw. It narrows the defect
to the interaction between animated mesh geometry and a non-zero camera
orientation; it does not yet distinguish sample deformation, placement,
projection or clipping. No source fix, Vulkan run, or release-scale 150-frame
acceptance is claimed.

## Plan hashes

```text
8c42045087ca0c44888d1761ad46cd26bff564b9ad74eadd3751c28de64ce391  moving-mesh-static-camera-zero.plan.json
8a6dafba4e97083795ca485828b12df638a6f1af6206c6e03b583c13790f14cf  moving-mesh-moving-camera.plan.json
93aecb85c942f5443a4a4032120acb01069d5111404c0a3ef5e2e7d8074a0b00  static-mesh-moving-camera.plan.json
172b43679d6c3af12ea6d995a748888a8ff711a3c00b96f3c4b8e446d251883b  moving-mesh-static-yaw.plan.json
```

## Source regression follow-up

Temporary tracing isolated the disappearance to camera far clipping: the mesh
processor used the fallback `Camera{}` far distance of 1000 while the resolved
RenderPlan camera specified 10000. Under animated positive X and non-zero yaw,
camera-space depth crossed that unintended limit. The direct lit/unlit
rasterizer controls passed. The fix propagates resolved near/far distances
through camera values, interpolation, projection context and software/native
mesh projection; cache fingerprinting includes both distances.

A typed RenderPlan software regression now renders every frame 0–149 at
640×360 with animated inline vertices and three camera samples (frames 0, 74,
149) combining small camera translations, pitch and yaw. The focused case
passed 301/301 assertions, including visibility on every frame. Focused camera
tests passed 9 and 34 assertions, and the focused low-level mesh regression
passed 8 assertions. Build command:

```bash
ninja -C build/chronon/linux-fast-dev -j12 chronon3d_camera_tests chronon3d_renderer_core_tests
```

The final expanded 150-frame regression was rebuilt and rerun locally. These
results are from a dirty worktree; they do not certify clean-build CI, native
Vulkan, a full camera orbit, the skinned+morph glTF corpus or human review.
P0-04 stays PARTIAL.

## Image-under-mesh regression (supersedes the compatibility-path canary)

The initial canary used `render_compiled`, a legacy compatibility evaluator,
and did not exercise the production `compiler::compile()` →
`renderer.render_program()` path. A minimized typed RenderPlan test reproduced
the defect: the isolated mesh and mesh over `plane3d` each covered 144 red
pixels, while the mesh over a 2D image covered 109 (35 missing, including 25
interior pixels). This was a real failure; the earlier PASS claim is withdrawn.

The reproduced cause was per-pixel fragment ordering: the image and mesh were
coplanar by contract but projected to depths 1000.0 and 1000.000061. Exact
floating-point ordering placed the mesh behind the image. The compositor now
groups fragments within its existing occlusion epsilon and restores authored
submission order inside each group. The typed software regression now passes
16/16 assertions with 144 red pixels for all three cases, zero edge coverage
mismatches, and zero interior loss. Build: `ninja -C
build/chronon/linux-fast-dev -j12 chronon3d_camera_tests`; focused run:
`build/chronon/linux-fast-dev/tests/chronon3d_camera_tests --test-case='2D
image behind inline mesh preserves the complete mesh coverage' --no-version`.

Remote Vulkan diagnostics from the prior compositor version confirmed the
native projected-image quad path and exact plane3d parity, but that binary
predates the epsilon grouping fix. Raw Vulkan output required GPU-to-host
readback and therefore did not satisfy strict-native mode; NVENC was unavailable
on the host probe. The current Vulkan output is being regenerated. This local
software PASS does not close the original showcase scene, strict-native video,
same-SHA CI, or human review; P0-05 remains PARTIAL.
