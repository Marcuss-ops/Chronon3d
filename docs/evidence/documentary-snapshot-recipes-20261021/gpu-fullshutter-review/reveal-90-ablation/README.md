# reveal-90 Vulkan diagnostic ablations

Three original 90°/8-sample Vulkan frame-53 plans isolate the blank photo
state. Each changes only the named plan property from the connected baseline:

| Plan | Single change | Observed result |
|---|---|---|
| `reveal90_plate-removed.plan.json` | Removed shape layer 102 (`DocumentarySnapshotFrame`). | Beige card disappears, but the photo remains absent and the background is gray. |
| `reveal90_effects-removed.plan.json` | Cleared the photo image layer's `effects` list. | Beige card remains; photo remains absent. |
| `reveal90_image-flipped.plan.json` | Changed the photo image rotation from Y=-90° to Y=+90°. | Beige card remains; photo remains absent. |

Each successful raw output, timing sidecar, and PNG projection is stored next
to its plan. These ablations do not identify the cause; they rule out each
single edit as a sufficient fix. The original visible card/absent-photo frame
remains the failure recorded in the parent review.

## Crop-flip follow-up

The connected image layer has `crop.flip_x=true`; the pack toggles this value
for `Reveal90` before emitting the plan. Two more original 90°/8-sample Vulkan
renders changed only the connected `crop.flip_x` value:

| Plan | Single change | Frame | Observed result |
|---|---|---:|---|
| `reveal90_crop-flip-disabled.plan.json` | `crop.flip_x=true` → `false` | 53 | Photo appears; strong temporal blur during the turn. |
| same plan | same | 120 | Photo appears sharp, but is horizontally mirrored relative to the software reference frame 120. |

This is not a valid recipe fix: changing the flag restores visibility on the
Vulkan readback lane while changing the photo's orientation. Native image
promotion explicitly rejects `flip_x` in `src/render_graph/nodes/source_node_native_image.cpp`;
the surface diagnostic and both raw frames are retained here. The connected
plan still fails its Vulkan visual gate pending correct flip support and a
visually matching result.

## Connected recipe correction

The generated pack plan cannot express a reflected world transform because
Motion→RenderPlan previously rejected all negative winding. The lowering now
preserves reflected transforms for image layers as a negative X scale, while
keeping the rejection for other content types. `DocumentarySnapshotPack`
leaves `crop.flip_x` unset and applies that transform to the Reveal90 image.
The regenerated `reveal90_source-fixed.plan.json` validates with five layers
and 121 frames; its image layer has scale X `-0.47554016` and no crop flip.

Vulkan raw frames 53 and 120 both contain the photograph with the same
orientation as the connected software reference frames. Frame 53 retains the
authored 90°/8-sample shutter blur; frame 120 is sharp and settled. Raw data,
timing sidecars, logs, and PNG projections are stored beside the plan. This
closes the sampled Reveal90 photo visibility/orientation review on Vulkan.
Strict `require_gpu_native` remains open: the frame-120 probe fails on the
decorative rectangular frame (`ShapeType::Rect`, type 1), so NVENC accepts no
frame. Removing all shape layers advances the strict job through image setup,
but it then exits with SIGSEGV before submitting a frame; that diagnostic log
and zero-byte partial MP4 are also retained here. These strict failures do not
invalidate the Vulkan raw visual samples.
