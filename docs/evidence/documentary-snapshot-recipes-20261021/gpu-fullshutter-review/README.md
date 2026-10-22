# Documentary Snapshot GPU recipe review — 2026-10-21

The connected 1920×1080 plans were rendered with their original 90°/8-sample
shutter at frame 53 using `--backend vulkan` and the RGBA raw sink. This is a
Vulkan render with readback; it is not strict native full-graph GPU/NVENC
certification. Each recipe directory contains the raw frame, PNG projection,
timing sidecar, and invocation log.

Visual contact sheets are `gpu-fullshutter-contact.png` and
`gpu-software-frame053-contact.png`. Software frame-53 samples are in the
parent recipe directories. Vulkan rendered a frame for all eleven recipes, but
two outputs were exactly zero RGBA and one showed a blank card instead of the
photograph:

| Recipe | Vulkan frame 53 observation |
|---|---|
| archive-crane-reveal | Visible; photo strip is washed out against software (RGB MAE 122.88). |
| corner-turn | Visible; photo crop and color differ substantially (RGB MAE 165.42). |
| filmstrip-handoff | Cards visible; close to software (RGB MAE 5.41). |
| focus-drop | The original capture was exactly zero RGBA; later frame-53 GPU weighted-add rendering is documented in `gpu-torture-canary/`. Current frame-90 recheck is non-black but still has a clipped title and heavy defocus. |
| foreground-photo-pass | Title-only state matches software (RGB MAE 1.18). |
| photo-stack | Photo visible and close to software at frame 53 (RGB MAE 4.83); the known frame-46/47 shutter mismatch and strict-native unsupported-shape failure remain. |
| pullback-reveal | Intentional title overscan is visible; image strip is at the top (RGB MAE 119.08). |
| push-through-snapshot | Red portal state is visible; color differs from software (RGB MAE 35.64). |
| reveal-90 | **CORRECTED:** the original baseline was a blank beige card; the negative-X image transform fix restores the photo with correct orientation in the follow-up frames below. |
| split-depth | The original capture was exactly zero RGBA. After correcting the cropped framebuffer/native backing extent mismatch, the 90°/8-sample frame-53 recheck and diagnostic frame-90 recheck both render visible content; full-sequence quality remains open. |
| whip-to-photo | Photo visible; close to software (RGB MAE 3.95). |

### DoF ablation for zero-RGBA outputs

For `focus-drop` and `split-depth`, a Vulkan A/B at frame 53 changed only
`camera.dof.enabled` from true to false, preserving the original 90°/8-sample
shutter, layers, camera samples, and frame. Both outputs changed from exactly
zero RGBA to visible full-frame content. Although the base camera declares
aperture 0, the authored frame-53 sample enables DoF with aperture 0.0064 and
reaches the same failing route.

This matches the renderer implementation: the native-depth-surface branch can
dispatch Vulkan DOF, but the CPU-depth branch calls
`VulkanBackend::apply_per_pixel_dof`, which rejects CPU depth with “native depth
surface required.” The raw render currently exits successfully while exposing
an all-zero output, so this is a renderer error-reporting and missing native
DOF implementation issue, not a missing asset. The ablation plans, raw frames,
PNG projections, and logs are under `dof-ablation/`.

### Strict native GPU/NVENC follow-up

Frame 53 was attempted with `--gpu-hot-path-mode require_gpu_native`, the
native encoder, and NVENC, with and without DoF. Before the host-pixel guard,
`focus-drop` SIGSEGVed in a oneTBB task for
`render_compiled_composition_frame_temporal`; the compact GDB trace is
`focus-drop/focus-drop_frame053_strict_nvenc_gdb.txt`. The exact source line
was unavailable in the release binary. Inspection found that both temporal
accumulation loops dereferenced `Framebuffer::pixels_row()` without checking
GPU-only residency. The loops now check for host pixels and fail closed with
an explicit `native weighted-add accumulator` capability error. Rebuilt
`chronon3d_cli` strict-native attempts for `focus-drop` and DoF-disabled
`split-depth` both report this error instead of crashing; NVENC accepts zero
frames. Logs are named `*_after-guard.log` in each recipe folder.

At the time of these attempts, temporal rendering accumulated weighted
samples through CPU `tbb::parallel_for`; the guard prevented a crash but did
not implement GPU temporal shutter. This finding is historical: Vulkan now
has a native weighted-add operation, and a later eight-sample/90° frame-30
probe of the original torture plan renders visibly through the FFmpeg/NVENC
pipe. See [`the later GPU probe`](../gpu-torture-canary/README.md). The full
strict-native multi-frame shutter sequence remains unverified; `split-depth`
with authored DoF also independently failed strict depth locality in these
captured attempts.

### Strict-native no-shutter control

Removing `temporal_motion_blur` from a copy of the plan makes the strict-native
`focus-drop` frame complete. NVENC accepts one 1920×1080 H.264 frame; the
decoded image has the expected title, caption, framed photo and face. The log
selects `required_gpu_native_full_graph` and reports zero conversion bytes and
zero encoder staging-copy bytes. The source plan and artifacts are
`focus-drop/focus-drop_dof-disabled_single-sample.plan.json` and
`focus-drop/focus-drop_frame053_strict_nvenc_single-sample.*`. A second
no-shutter render using the authored DoF plan also completes, but its decoded
frame is pixel-identical to the DoF-disabled result (MAE 0); it does not prove
visible DoF behavior.

These earlier one-frame controls sampled the wrong timeline position and are
superseded by the corrected full-timeline strict-native results in the final
section below. They do not describe the current recipe output.

`gpu-transition-review/` now contains Vulkan raw samples at frames 45, 46, 53,
54, 90, and 120 for all eleven recipes. Frame 90 was missing from the first
contact sheet; it has now been GPU-rendered for all eleven, each yielding a
full 8,294,400-byte RGBA frame. Frames other than 53 use a diagnostic 1°
shutter and 2 samples to make boundary and settle review practical; they do not
replace the full-shutter sample. The updated visual review is
`gpu-transition-review/gpu-transition-contact-all-v2.png`.

At diagnostic 1°/2-sample frame 90, the prior captures showed `focus-drop`
and `split-depth` black, and baseline `reveal-90` as a beige card without a
photograph. A current Vulkan recheck now shows the `focus-drop` portrait, but
it remains heavily defocused with a partly clipped title; it does not close
its visual gate. The earlier `foreground-photo-pass` white frame was
rechecked on Vulkan after renderer updates; it now has a dark background and a
large, clipped title, so it remains a visual failure. See the recheck artifacts
in `gpu-transition-review/foreground-photo-pass/`.
`archive-crane-reveal`, `corner-turn`,
`filmstrip-handoff`, `photo-stack`, `pullback-reveal`, `push-through-snapshot`,
and `whip-to-photo` show visible content at this sample, with the transitions
and framing captured in the contact sheet. These are Vulkan GPU readback
observations, not strict-native NVENC certification.
The baseline Reveal90 frame-90 cell is superseded by a source-fixed Vulkan
full-shutter render: the correctly oriented photo and `ROME, 1986` caption are
visible at frame 90. Its raw frame, PNG, and log are in
`reveal-90-ablation/reveal90_source-fixed_frame_090.*`; v3 of the transition
contact sheet uses this corrected sample while retaining the baseline files.

The authored 90°/8-sample DoF-off probe below was captured before the native
rect fast-path correction and is superseded for single-sample output by the
corrected strict-native renders below. GPU weighted-add now produces
reviewable authored-shutter samples through the Vulkan raw/pipe routes. The
remaining shutters gates are full-sequence GPU verification and direct-surface
NVENC visual correctness.

The baseline contact sheets above predate the Reveal90 fix and intentionally
retain the original blank-card capture. The corrected full-shutter frame 53
and settled frame 120 were reviewed together in
[`reveal90_source-fixed_review-contact.png`](reveal-90-ablation/reveal90_source-fixed_review-contact.png): the photograph is visible in both, blurred during the 90° turn, sharp and correctly oriented at frame 120. This supersedes Reveal90's baseline row above; the old source capture remains unchanged.

Reproduce the full-shutter sample for a recipe with:

```bash
chronon3d_cli render --backend vulkan --plan PLAN.json \
  --assets-root WORKSPACE --video-sink raw --pipe-pixfmt rgba \
  --start-frame 53 --end-frame 53 -o frame_053.rgba
```

This review exposes Vulkan visual failures and does not close the full-shutter
or strict-native documentary gates. The full 601-frame torture render remains
open.

## Native radial-fill correction and strict-native visual review

The washed-out frames came from `SourceNode`'s native rect fast path: it
accepted non-solid fills but drew `RenderNode::color` as an opaque solid,
bypassing the gradient payload and Vulkan shape dispatcher. The fast path now
accepts only enabled solid fills. Pad-spread radial rectangles with two or
three stops reach the Vulkan native radial shader branch. A strict-native
one-frame NVENC plan with a black base and the radial fill now shows the warm,
transparent falloff (`focus-drop/debug-radial-fixed.png`).

After the guard fix, DoF-disabled, single-sample FocusDrop and SplitDepth plans
both completed all 121 frames through `require_gpu_native` + NVENC. Each log
reports `required_gpu_native_full_graph`, 121 accepted H.264 frames, and zero
conversion and staging-copy bytes. The decoded actual frame 90 in each plan
shows the portrait, caption, and card clearly; SplitDepth also retains its
tilted card. Images are `focus-drop/focus-drop_frame090_strict-native_fixed.png`
and `split-depth/split-depth_frame090_strict-native_fixed.png`; the MP4s and
logs beside them contain the complete runs.

A later visual audit of these decoded strict-native PNGs found a large brown
elliptical field covering much of the black negative space in both recipes.
The portrait, caption, and card remain visible, but these are not visual passes;
the earlier single-sample signoff is withdrawn. The authored shutter was also
open at the time of these historical renders. Current native weighted-add
probes are recorded below, but direct-native encoding remains visually wrong
with and without temporal accumulation. The full 601-frame torture render and
the separate PhotoStack strict-native shape gate also remain open.

The same 0–120 Vulkan run was repeated for the DoF-disabled SplitDepth plan.
Its extracted frame 90 (`split-depth/split-depth_frame090_radial-native.png`)
also shows a nearly washed-out photo and background, confirming the visual
failure is shared across both recipes. The full-timeline log is
`split-depth/split-depth_gpu-radial-review.log`.

### Authored full-shutter raw Vulkan review (2026-10-21)

The earlier statement that temporal accumulation fails on GPU-only surfaces
applies to the **strict-native NVENC** route: its accumulator still requires
CPU host pixels and fails closed. A separate Vulkan raw-sink run with the
DoF-disabled, authored 90°/8-sample plan completed all 121 frames for
FocusDrop. It wrote exactly 1,003,622,400 bytes (121 × 1920 × 1080 × 4), and
the timing sidecar reports 121 timestamped GPU frames, 1,741 ms total GPU
device time, and 359,376 ms render time (0.324 fps). This confirms the Vulkan
GPU renderer is active, while CPU-side accumulation/readback dominates wall
time. The reviewed frame 90 is
`focus-drop/focus-drop_frame090_fullshutter_vulkan.png`: the portrait and
`ROME, 1986` caption are readable, the corrected radial leak is translucent,
and the title is partly above the canvas during the transition. This is a raw
Vulkan visual pass for the sampled full-shutter frame, not strict-native
zero-copy certification. The full raw sequence, log, and timing sidecar are
retained beside it.

The matching DoF-disabled SplitDepth plan also completed all 121 frames on
Vulkan raw. Its file is exactly 1,003,622,400 bytes; the sidecar reports 121
GPU-timestamped frames, 1,804 ms total GPU device time, 338,781 ms render time
(0.349 fps), and zero CUDA host-upload bytes. The reviewed frame 90 is
`split-depth/split-depth_frame090_fullshutter_vulkan.png`: caption and portrait
are readable, the card retains its tilt, and the frame is not washed out. This
closes the sampled raw full-shutter visual gate for SplitDepth. At capture
time, both runs used expensive CPU temporal accumulation/readback and did not
close strict-native NVENC zero-copy shutter certification.

These 121-frame captures predate the GPU weighted-add implementation and used
CPU accumulation. They remain valid historical visual evidence, but do not
verify the new GPU accumulator or its performance. The current frame-30 GPU
probe and its direct-surface NVENC failure are documented in the linked
canary; full-sequence GPU shutter verification remains open.

### FocusDrop GPU temporal sample (2026-10-21)

The original FocusDrop recipe was rendered at frame 53 with its authored
90°/8-sample shutter and DoF enabled, using Vulkan plus the FFmpeg/NVENC pipe.
The decoded image
[`focus-drop_frame053_gpu-weighted-add.png`](focus-drop/focus-drop_frame053_gpu-weighted-add.png)
was visually reviewed against the existing software sample: title and layout
remain readable, the portrait stays in its card, and the face is visibly
defocused. The timing sidecar records 48 GPU submissions and 14.857 ms of
timestamp-executed Vulkan work for this frame. This closes a focused raw-GPU
render/readback sample for FocusDrop; it does not certify strict-native
direct-surface NVENC, all 121 frames, or cross-backend pixel parity. The
original plan, MP4, timing sidecar, and command log are beside the PNG.

### Split-depth native surface extent recheck (2026-10-21)

`PerPixelDofNode` cloned a cropped logical framebuffer (191×60) into a surface
with the same cropped extent even though the source native Vulkan backing was
1920×1080. Native copy rejected the incompatible sizes. The clone now allocates
from the registered source surface description and preserves the framebuffer's
logical crop/origin separately. The Vulkan diagnostic frame 90 and authored
full-shutter frame 53 both now produce complete frames. Both PNGs were visually
reviewed: frame 90 has the portrait card, caption, and black negative space;
frame 53 shows the card and title during the transition. Artifacts are in
`split-depth/frame_053_recheck.*` and `gpu-transition-review/split-depth/frame_090_recheck.*`;
this closes the black-frame symptom for these samples only, not the full
sequence or strict-native encoder gate.

### Split-depth strict-native NVENC temporal recheck (2026-10-21)

After fixing the cropped native-surface extent, the first strict-native retry
exposed that the per-sample frame batch was ending before weighted accumulation
could read the DOF clone surface. The temporal graph now keeps the Vulkan batch
open through `accumulate_surface_weighted()` and closes it immediately after
recording the add. The strict `require_gpu_native` + native NVENC frame-53 run
then completed: 8-sample weighted accumulation finalized into encoder surface
3, NVENC accepted one H.264 frame, and conversion/staging-copy bytes remained
zero. The decoded PNG `split-depth/frame_053_strict-native_recheck3.png` still
shows the oversized brown shape, while the corresponding Vulkan raw frame
above shows the card and title against black. This post-fix result confirms the
remaining defect lies in the strict-native encode output path or its final
surface handoff, not in sample rendering or the DOF clone extent. The direct
NVENC visual gate remains open.

### NVENC RGBA alpha-matte correction (2026-10-21)

The former brown-field diagnosis blamed radial-gradient evaluation. A host-side
Vulkan dispatch trace proved the radial stop values were correctly premultiplied
by animated opacity. The actual defect was in
`rgba_surface_to_nv12_2x2`: RGBA32F framebuffer RGB is linear and premultiplied,
but the CUDA converter divided RGB by alpha before encoding a target with no
alpha plane. That turned low-opacity colors into opaque colors. The CPU packed
converter already composites premultiplied RGB over black for the same video
boundary.

The CUDA converter now transfer-encodes the premultiplied RGB directly, which
composites transparent pixels over the black video matte. After rebuilding
`chronon3d_cli -j16`, the original strict-native full-shutter frame-90 export
accepted one H.264 frame with zero conversion/staging bytes. Its dark-field
samples now track the Vulkan raw reference closely: top center `(27,14,2)` vs
`(29,15,3)`, lower center `(6,1,0)` vs `(6,2,1)`. A strict-native single-sample
frame-90 export is sharp and shows the same dark falloff. The MP4, decoded PNG,
and logs are `split-depth_frame090_strict-native_alpha-matte-fix.*` and
`split-depth_frame090_strict-native_alpha-matte-fix_single-sample.*`.

The strict-native full-shutter image is still softer than the saved raw Vulkan
full-shutter reference around the face and title. This color/alpha correction
therefore closes the brown-field conversion defect at this SplitDepth sample,
not DoF parity, the other recipe rechecks, or the full timeline. Historical
brown-field controls above are diagnostic evidence for the fixed conversion
bug, not evidence of a remaining radial-shader defect.
