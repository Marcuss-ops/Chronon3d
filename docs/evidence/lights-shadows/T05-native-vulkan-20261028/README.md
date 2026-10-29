# T05 native Vulkan render — 2026-10-28

## Result

The one-frame T05 C3D plan rendered on the NVIDIA RTX A4000 in strict
`require_gpu_native` mode and encoded with native NVENC. The plan resolved to
`required_gpu_native_full_graph`; the log reports four native `Shadow` nodes,
one frame accepted by NVENC, and one muxed packet. The MP4 was kept at
`/tmp/chronon-t05-native-shadow.mp4` during the run (SHA-256
`b7dcf1af73707924f81e5b0686fca02fa139dad65362c53fa30a0eaf22445a81`); the
decoded 1920×1080 PNG is preserved here as `strict-native-frame.png`.

Command:

```sh
./build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli render \
  --plan docs/evidence/lights-shadows/T05-c3d-production-20261022/t05-production.plan.json \
  --start-frame 0 --end-frame 0 --output /tmp/chronon-t05-native-shadow.mp4 \
  --assets-root "$PWD" --backend vulkan \
  --gpu-hot-path-mode require_gpu_native --hardware nvenc \
  --encoder-backend native --log-level info
```

The CLI and encoder path were operational on this host; this is not a clean
same-SHA build or WBH certification. The source tree was based on
`cfdd5b15dcba7d787df0d2f2c088af989ed55316` with local edits. Only the touched
CLI objects were rebuilt; the full build was not run.

## Review and remaining gate

The decoded image shows the floor and casters but no clearly visible cast
shadow. Therefore this is evidence that the native graph and export complete,
not evidence of acceptable shadow quality. A subsequent strict shadow-off
render also exited successfully and NVENC muxed one frame, but its decoded
1920×1080 image is completely black (RGB mean 0; preserved as
[`caster-off-black-control.png`](caster-off-black-control.png)). Its plan is
[`caster-off-control.plan.json`](caster-off-control.plan.json). The strict caster-on image is byte-identical
to `strict-native-frame.png` (SHA-256
`5aa35f3717a4967af66f69bbcb65c328a2c243b219f7a818f350b40e235e2d67`). The
on/off CLI logs are preserved as [`caster-on-render.log`](caster-on-render.log)
and [`caster-off-render.log`](caster-off-render.log); both report
`required_gpu_native_full_graph` and one accepted NVENC frame. The logs show
native surface extent replacement for 218×287 and 336×395 cropped
surfaces, with no Vulkan error. This pair cannot establish shadow quality;
the black control still needs diagnosis. A production-shot review and GPU
particle instancing are also open, so T05 remains PARTIAL.

The working-tree diagnosis is that `acquire_owned_fb()` can copy a GPU-only
surface handle onto a second live framebuffer. `ensure_empty_native_surface()`
then cleared or released that shared handle while preparing the destination.
The local fix detaches the possibly shared handle and creates a new transparent
surface; a Vulkan regression now checks that Lighting leaves its input surface
unchanged. The affected implementation objects and regression-test translation
unit compile with Ninja `-j12`. The regression has not been linked or run, and
the CLI frame above predates this fix; a new strict-native on/off render is
still required before the cause can be considered resolved.

The caster-off control was run with the same strict backend and frame range:

```sh
./build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli render \
  --plan docs/evidence/lights-shadows/T05-native-vulkan-20261028/caster-off-control.plan.json \
  --start-frame 0 --end-frame 0 --output /tmp/chronon-t05-caster-off-consistent.mp4 \
  --assets-root "$PWD" --backend vulkan \
  --gpu-hot-path-mode require_gpu_native --hardware nvenc \
  --encoder-backend native --log-level info
ffmpeg -y -v error -i /tmp/chronon-t05-caster-off-consistent.mp4 \
  -frames:v 1 /tmp/t05-off-consistent.png
```

## Follow-up isolation — 2026-10-28

The current working-tree CLI was rebuilt and the black control was repeated.
The software render of `caster-off-control.plan.json` is not black (decoded
RGB mean 36.601; 6,219,733 nonzero channel values), so the plan itself has
visible content. Strict Vulkan remains black. Prefix renders narrow the trigger:

| Strict Vulkan layers included | Decoded RGB mean | Result |
| --- | ---: | --- |
| Backdrop | 13.667 | visible |
| Backdrop, floor | 34.356 | visible |
| Backdrop, floor, hero caster | 35.774 | visible |
| Backdrop, floor, both casters, flags false | 0–36.617 across repeats | intermittent black |
| Full off plan, with directional light and both casters disabled | 0 | black |

The exact four-layer off plan with both caster flags false has produced both
visible and black frames across repeated strict renders (three visible and three
black observations so far); the plan file hashes match. In the full off plan,
re-enabling either caster (or both) made that individual output non-black.
The repeated four-layer result means the issue is nondeterministic and cannot
be explained by light/caster flags alone. It points to a native graph/surface
lifetime or scheduling issue. The individual failing node is not yet identified;
the frame A/B and shadow-quality gate remain open. T05 stays **PARTIAL**.

## Follow-up — clone authority and repeatability, 2026-10-28

Added diagnostics to `LightingNode` show that the caster-off control computes
the same positive multiplier for floor and both casters:
`(0.5790, 0.5303, 0.4549)`, with world normal `(0, 0, 1)` and one directional
point light. The black output is therefore downstream of lighting evaluation.

The GPU-only framebuffer clone in `acquire_owned_fb()` was copying native
surface handles for planned slots, while the unplanned clone path still aliased
the input handle. The latter path now allocates an independent native surface
and copies into it when the backend and registry are available. The CLI build
passes; the planned-slot authority regression passes (1 case / 7 assertions),
and the strict Vulkan directional-lighting regression passes (1 case / 14
assertions). This change is not yet accepted as the T05 fix: the off plan
produced 3 visible frames and 1 all-black frame across four identical default
scheduler runs. With the sequential scheduler it produced 3/3 black frames.
The on plan produced a non-black frame (RGB mean 53.517, max 192). More
node-level surface-content evidence is required to isolate the remaining
intermittent/lifetime behavior; the A/B and T05 remain open.

An additional backend regression now exercises `acquire_owned_fb(source)`
without a planned slot against a real Vulkan surface. It verifies a distinct
destination handle, source validity, and exact RGBA pixel equality on both
surfaces (1 case / 12 assertions). This validates the clone operation itself;
it does not resolve the full graph's intermittent black frame.

The optional `CHRONON3D_NATIVE_SURFACE_PROMOTION_DIAG=1` output readback was
extended to measure the NVENC surface after the graph batch closes. In the
sequential all-black render, Vulkan already contains RGB mean 0 / max 0 before
encode. A visible default-scheduler render measured mean 0.083828 / max
0.515357. This rules out NVENC as the origin of the black frame; the remaining
failure is inside graph execution or its native surface/resource lifetime.

## Follow-up — strict-native reruns and transform isolation, 2026-10-28

The clone fix and auxiliary-copy API passed the backend regressions recorded
above, but the repeated end-to-end acceptance still fails. With the candidate
that keeps the Composite seed copy in its original planned path, 5 strict
`caster-off` renders produced 3 expected frames, 1 backdrop-only frame (RGB
mean 13.667), and 1 black frame; the `caster-on` series produced 3 expected
frames and 2 degraded frames. A separate 8-render trace series produced only
2 expected off frames, with the others ranging from black to RGB mean 39.049.
These traces are diagnostic runs, not acceptance evidence.

An experiment making the Composite seed copy auxiliary was worse (2/9 early
off renders black and multiple degraded frames) and was reverted. A second
experiment that counted auxiliary copies as normal dispatch ordinals also
failed to improve repeatability and was reverted. Reducing CLI workers with
`CHRONON3D_THREADS=1` made the failure deterministic: 5/5 off renders were
black and 5/5 on renders contained only the backdrop (RGB mean 13.667).

The per-node content snapshot probe showed that source color surfaces can be
valid while some affine Transform outputs are zero; the corresponding source
and destination Vulkan images are distinct and transform parameters are
consistent. The failure is therefore downstream of color-source creation and
can occur before Lighting or NVENC. Vulkan synchronization validation was
enabled for three more renders; it emitted no `VUID` or `SYNC-HAZARD` messages,
but two outputs were black. No definitive cause or fix has been established.
T05 remains **PARTIAL**; repeatable caster-on/off output, shadow-quality
review, particle C2, and same-SHA CI are still open.

Reversing the deterministic execution order within each graph level did not
change the sequential failure: 5/5 caster-off renders remained fully black.
This rejects a simple forward-versus-reverse node ordering explanation; the
intermittent default-scheduler behavior still points to a concurrency-sensitive
resource or synchronization defect, which has not yet been isolated.

## Follow-up — Transform/content snapshot rerun, 2026-10-29

The current fast-dev CLI completed one strict Vulkan/NVENC `caster-off` render
with `CHRONON3D_T05_TRANSFORM_DIAG=1` and
`CHRONON3D_T05_SURFACE_SNAPSHOT=1`. The command exited successfully and
NVENC accepted one frame. Full log: `../T05-native-vulkan-20261029/diagnostic-off.log`.

All four affine dispatches reported initialized, distinct-sized source and
destination surfaces, valid non-empty dispatch bounds, and `command_plan=false`.
The frame-end copy snapshots narrowed the visible-content loss further:

| Graph node | Snapshot result |
| --- | --- |
| Floor Transform (1724×377) | RGB mean 0.433264, max 0.538236 |
| Floor Lighting | RGB mean 0, max 0 |
| Hero Transform (218×287) | RGB mean 0.221241, max 0.433880 |
| Hero Lighting | RGB mean 0, max 0 |
| Secondary-caster Transform (336×395) | RGB mean 0.290416, max 0.890006 |
| Secondary-caster Lighting | RGB mean 0.166338, max 0.515357 |

The backdrop-only final composition measured RGB mean 0.015355/max 0.515357.
This run shows that the relevant floor/hero inputs can contain pixels after
Transform while their later Lighting snapshots are zero; the secondary caster
Lighting output remains visible. The empty 1408×595 Transform is fed by a
transparent Clear output and is not, by itself, evidence of lost authored
content. This narrows the next inspection to the Lighting read/write/lifetime
boundary, but does not yet prove a cause: snapshots are taken after graph
recording and this is one diagnostic render. The no-snapshot repeatability
gate was subsequently rerun as follows.

## Repeated no-probe acceptance — 2026-10-29

The current fast-dev CLI ran five strict Vulkan/NVENC renders per mode with no
T05 probes. All ten encoded and decoded successfully. The five `caster-off`
frames were byte-identical after decode (RGB mean **53.180168/255**, one
decoded RGB hash); the five `caster-on` frames were all visibly complete, with
RGB means **53.445176–53.529876/255** and three decoded RGB hashes. None was
black or backdrop-only. This is 5/5 complete outputs in each mode, compared
with the 28 Oct diagnostic rerun's 3/5 complete off and 3/5 complete on.

The first on/off decoded pair differs by mean absolute channel value
**0.470675/255**; 105,575/2,073,600 pixels differ by more than 3 code values,
inside the subjects/floor region (`bbox 508,215..1328,685`). The cast shadow is
present but subtle in the full-resolution visual review. The 29 Oct probe run
still recorded zero at two intermediate Lighting snapshots, so the root cause
is not explained by this passing no-probe series. The repeatability gate passes
for these five reruns on the current tree; a general lifetime-black fix is not
claimed. Per-run MP4s, logs, decoded hashes and RGB metrics are in
`../T05-native-vulkan-20261029/` and `acceptance-results.json`.

At measurement time the RTX A4000 reported 8.0 GiB used of 16 GiB and 0–1%
GPU utilization between renders; this host also had the existing Ollama server
and three Chronon daemons resident. No other render jobs were started in
parallel with the acceptance sequence.
