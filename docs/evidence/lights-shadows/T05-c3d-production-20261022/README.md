# T05 C3D shadow integration gate — 2026-10-22

## Result: PARTIAL, not certified

The C3D software RenderPlan renders valid 1920×1080 RGBA frames. A matched
caster-on/caster-off A/B changes receiver-floor pixels (see
`shadow_metrics.json` and `T05_c3d_shadow_contact.png`), so the effect reaches
the C3D compositor. The measured floor mean RGB-L1 change is 0.894 at the
original depth separation; the visual shadow is weak. `depth-gap-review/`
records a second matched A/B after reducing receiver depth separation; this
does not improve the measured contrast. It is a diagnostic fixture, not an
approved production shot.

The strict Vulkan native attempt used the same shadowed RenderPlan with
`--backend vulkan --gpu-hot-path-mode require_gpu_native`. It failed closed at
the `Shadow` graph node with:

```text
UnsupportedCapability: shadow requires a native GPU pass or CPU pixel backing
```

That failure matches the implementation contract in
`include/chronon3d/render_graph/nodes/shadow_node.hpp`: `ShadowNode` is a CPU
reference and does not read back GPU-only caster pixels in strict-native mode.
The threepp GL PBO→CUDA→NVENC canary therefore does not prove integration in the
C3D production renderer.

## Closure gates still open

1. Implement or integrate a native C3D shadow pass that works with the strict
   RenderPlan GPU graph.
2. Establish a production shot and approve the base composition before
   effects, then review the shadowed result.
3. Record a matched A/B with an agreed visual/quantitative quality threshold,
   plus strict-native repeatability and validation evidence.

T05 remains **PARTIAL**. No ticket is marked closed by this evidence.
