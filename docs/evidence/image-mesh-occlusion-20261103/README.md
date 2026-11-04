# P0-05 typed image/mesh occlusion diagnostic

## Fixture and regression

This diagnostic compares the same inline red triangle in three typed
RenderPlans: alone, over a flat 2D blue image, and over the same image authored
as a `plane3d`. The source test generates `output/camera_motion/image_mesh_occlusion/background.png`;
the preserved 128×96 copy is `background.png`. Frozen plans are
`mesh-over-image.plan.json` and `mesh-over-plane3d.plan.json`.

The production test path is `compiler::compile()` followed by
`renderer.render_program()`. The prior legacy `render_compiled` test did not
cover this path and its PASS is withdrawn. Typed software reproduced the bug:
isolated/image/plane mesh coverage was 144/109/144 red pixels, with 35 missing
pixels in the 2D-image composition, including 25 interior pixels.

The fix groups per-pixel fragments whose depths differ by no more than the
existing occlusion epsilon, then restores submission order inside each group.
This avoids an epsilon comparator that would violate strict weak ordering.
After the fix the typed regression reports 144/144/144 red pixels, zero edge
coverage mismatches, zero interior mismatches and passes 16/16 assertions.

Build and focused software test:

```sh
ninja -C build/chronon/linux-fast-dev -j12 chronon3d_camera_tests
build/chronon/linux-fast-dev/tests/chronon3d_camera_tests \
  --test-case='2D image behind inline mesh preserves the complete mesh coverage' \
  --no-version
```

## Raw diagnostic artifacts

`software-image.rgba` and `software-plane3d.rgba` are earlier raw 128×96 RGBA
captures and document the failing software state: respectively 109 and 144
classified red pixels. The post-fix outputs are
`software-image-postfix.rgba` and `software-plane3d-postfix.rgba`; hashes are
`6821f682fba2726ab3903c1c2ffae755a48b15be54af103cbfb4b397f72b3a20` and
`efef5e9845f2cb7cd6a173c65ac6b0969db7688ba4e254260719555fc8b1b3cb`.
Post-fix remote Vulkan outputs are `vulkan-image-postfix.rgba` and
`vulkan-plane3d-postfix.rgba`. Each exactly matches its corresponding
post-fix software output byte-for-byte (same hashes). All are 128×96 RGBA8
pre-encode artifacts, 49,152 bytes each.

The post-fix Vulkan runs used the A4000 with `--backend vulkan`,
`--gpu-hot-path-mode auto`, and raw output. They exercise Vulkan rendering,
but the raw sink reads the result back to host and therefore does not prove
the required no-readback strict-native video gate. The raw run passed after
the updated binary hash `1ea769de1f4dd8ea12d5fd7914dcb450f2e6ec17e0e829b4c7a3d4c1d1075db8`
was verified on the remote host. The earlier remote runtime probe reported
NVENC unavailable. Neither run proves the required strict-native Vulkan/NVENC
video gate. The remote alias is
`gpu-node`, NVIDIA RTX A4000 (16,376 MiB); see
[`REMOTE_GPU_WORKFLOW.md`](../../reference/REMOTE_GPU_WORKFLOW.md).

### Current shader parity rerun — 2026-11-04

After camera-space native occlusion was enabled for projected images, the
current GPU binary initially differed on the image fixture: 49 pixels that
were red in the established golden became blue. The plane3d case remained
unchanged. Diagnosis found `occlusion_tag.comp` used a strict depth comparison
while the CPU compositor accepts a later layer up to `1e-3` farther and keeps
the nearest accumulated depth. The shader now uses the same tolerance and
nearest-depth update.

Built with `ninja -C build/chronon/linux-video-fast-dev -j12 chronon3d_cli`.
The stripped binary was SHA-256
`f3f4010eb13e19e6d5c9bbf440fe7dabb6cbf07d7f090fd3e643cfd18a31486c` and was
run on `gpu-node` with `VK_LAYER_KHRONOS_validation`, Vulkan RTX A4000, raw
sink and host readback. Both new captures match their software post-fix
goldens byte-for-byte:

| Fixture | Vulkan SHA-256 | Software SHA-256 | Classified red pixels |
|---|---|---|---:|
| Image | `6821f682fba2726ab3903c1c2ffae755a48b15be54af103cbfb4b397f72b3a20` | same | 144 |
| `plane3d` | `efef5e9845f2cb7cd6a173c65ac6b0969db7688ba4e254260719555fc8b1b3cb` | same | 144 |

Raw captures, timing sidecars and run logs are preserved as
`vulkan-{image,plane3d}-epsilon-20261104.*`. Neither run logged a Vulkan
validation error. This closes the minimized P0-05 image-vs-plane fixture on
the current local build; original showcase, strict-native video, same-SHA CI
and human review remain release gates.

## Status and limits

P0-05 is **PASS for the minimized typed fixture / PARTIAL for release**. The
typed software regression and current Vulkan raw captures match for both
minimized plans. Required follow-up: exercise the original mixed showcase
scene and strict-native video path; run same-SHA CI; obtain human visual
review. Raw-readback diagnostics must not be reported as strict-native
release acceptance.

## P0-06 camera-space depth order probe

Two additional 128×96 typed RenderPlans contain the same red triangle
crossing the blue image plane, with the image and mesh authored in opposite
orders. The two software raw outputs are byte-identical (SHA-256
`0fecc3a872256fac7dd9a6500a83725e9a2057cae47a851c89854fa926da9efe`; 84 red
and 130 blue classified pixels). The remote RTX A4000 Vulkan raw outputs are
not: plane-first SHA-256 `ed2a6ce92a4c6de008da5364ceb9ec056957b35adfa905da9810361a6844b942`
(144 red, 70 blue), mesh-first SHA-256
`fe367aea90fe30f5ba562c14e44ce92853f9127ccda85b10bf0fe6968c033368` (22
red, 192 blue), with 122 differing pixels. These captures are host-readback
diagnostics, not strict-native release evidence.

The first Vulkan probe was **FAIL**: its two orders differed. Trace then
showed the image compositor receives a tight 20×16 surface at canvas origin
(54,40), while the depth and visibility mask are full-canvas. The previous
native occlusion path rejected the tight surface. The fix stamps its coverage
into full-canvas depth using that origin and makes the matte sample the
matching canvas region.

After the fix, `vulkan-region-plane-first-postfix.rgba` and
`vulkan-region-mesh-first-postfix.rgba` are byte-identical to each other and
to both software captures: SHA-256
`0fecc3a872256fac7dd9a6500a83725e9a2057cae47a851c89854fa926da9efe` for all
four. Vulkan output has 84 red and 130 blue classified pixels, with no
backend error in either run. The final trace-free CLI was built locally,
stripped and hash-verified on `gpu-node` (SHA-256
`e980d8545a1c1b354b391096f0e4acdebcad85eeb0cc4943019285a0d25ab133`); this
used `--backend vulkan`, `--gpu-hot-path-mode auto`, and the raw sink. It
proves this minimized order-permutation case with readback, not strict-native
video or the wider Mixed 3D corpus.

P0-06 is **PASS for this minimized Vulkan fixture / PARTIAL overall**. On the
The Vulkan backend registry target was built in `linux-video-test` with
`CHRONON3D_ENABLE_VULKAN=ON`, `CHRONON3D_ENABLE_CUDA_INTEROP=OFF` (CUDA headers
are absent on the dev host), and `-j12`. On the GPU host,
`Vulkan alpha and luma matte match the CPU coverage formulas` passed 1 case /
25 assertions, including the offset matte check; the existing
`Vulkan native occlusion holds a mesh half behind a 2.5D plane in both
authoring orders` passed 1 case / 75 assertions. Backend test executable
SHA-256: `808e25f241fa8e268d760999f628875d7fad97f5f42f414ad942f1a4710f7961`.
The camera software suite passed 91 cases / 67,793 assertions, renderer core
207 / 74,303 and render graph 447 / 19,566 before the offset-matte change.
Still required: wider camera/depth permutations, strict-native video,
same-SHA CI and the Mixed 3D acceptance corpus before promoting the ticket.

### Current epsilon-shader order-permutation rerun — 2026-11-04

Using the current `linux-video-fast-dev` CLI and the updated native depth
epsilon, both `mesh-crossing-mesh-first` and
`mesh-crossing-plane-first` were rerendered on Vulkan RTX A4000 with the
validation layer and raw host readback. Both Vulkan outputs, both new software
captures, and the frozen software goldens have SHA-256
`0fecc3a872256fac7dd9a6500a83725e9a2057cae47a851c89854fa926da9efe` (84 red,
130 blue pixels). The pre-epsilon GPU captures differed; the new captures
show the layer-order result survives the CPU/GPU depth-tolerance alignment.
Logs and raw captures are `vulkan-p006-*-epsilon-20261104.*`.

This reconfirms P0-06's minimized valid-permutation fixture on the current
build. Full plane/mesh/text/image permutations, strict-native video, CI and
the Mixed 3D corpus remain release gates.

## P0-07 alpha/depth sub-gate — 2026-11-04

The `linux-video-test` backend registry tests were built with Vulkan enabled
and CUDA interop disabled (the local host lacks CUDA development headers),
then run on the A4000 with `VK_LAYER_KHRONOS_validation`. The test executable
SHA-256 is
`58561e356cd0a9e58dea1814f1795dc5712261695e25c2d5959f896637ce4fb6`.
`Vulkan inline mesh rasterizes indexed UV triangles with alpha cutoff and
source-over` passed 23/23 assertions; `Vulkan alpha and luma matte match the
CPU coverage formulas` passed 25/25; the native mesh/plane occlusion test
passed 75/75. Full logs are
[`p007-alpha-depth-tests-20261104.log`](p007-alpha-depth-tests-20261104.log).

This is partial P0-07 evidence: it exercises cutoff discard, premultiplied
fractional source-over and a plane/mesh depth case, but it does not yet provide
the required pixel goldens for opaque/cutoff/fractional materials across two
meshes and two planes or establish production Z-write/order semantics. Keep
P0-07 **PARTIAL** until those cases are explicit and reviewed.

## P0-09 lifecycle sub-gate — 2026-11-04

On the A4000/Vulkan setup with validation enabled,
`Vulkan surface release waits for asynchronous work` passed 4/4 assertions and
`Vulkan frame batch descriptor allocator grows past the first chunk` passed
106/106. Three additional cases passed: async upload timeline ticket/readback
7/7, frame-batch wait on async upload 50/50, and ordered three-asset upload
tickets 14/14. The five tests total 181 assertions. See
[`p009-lifecycle-tests-20261104.log`](p009-lifecycle-tests-20261104.log).
The async cases are in
[`p009-async-tests-20261104.log`](p009-async-tests-20261104.log).
The rebuilt test executable SHA-256 is
`07d2175fcd48c006bbdfcbcc4b978b3603164e480ec67ce9c9a340199d19e7c4`.
The same validation-enabled A4000 binary also passed
`Vulkan surface reuse survives 100 async upload/readback/release cycles`:
702/702 assertions. It reused one surface handle through 100 create, async
upload, wait, readback, compare and release cycles. See
[`p009-100-cycles-20261104.log`](p009-100-cycles-20261104.log). This is a
surface lifecycle stress sub-gate, not 100 complete render jobs; cancellation,
reset, teardown, lifecycle telemetry and release CI remain open. P0-09 remains
**PARTIAL**.
