# T16 raw Vulkan shutter readback — root cause and fix

Date: 2026-10-22  
Plan: `../t16-shutter-center75.plan.json` (one output frame representing the
center T16 camera sample; temporal shutter 90°, 8 samples)  
Host: NVIDIA RTX A4000, Vulkan 1.3, Chronon3D `main` worktree

## Root cause

The Vulkan renderer produced valid native surfaces. The raw sink was handed a
CPU framebuffer before the temporal GPU work had been submitted and completed.
Two routing conditions combined:

1. `prepare_frame()` enabled native-surface retention from the resolved
   Vulkan/NVENC plan, even when the selected sink was `raw`, which has no native
   surface encoder consumer.
2. Isolated temporal samples defer `finish_frame_encode()` so the parent can
   add each sample to the Vulkan accumulator before closing that frame batch.
   The compositor allocated its sample output only when the *outer* job had a
   native encoder output. For `raw`, there was no sample handle and therefore
   no `DeferredNativeSampleBatch` owner to close the deferred batch. The final
   host readback ran against an accumulator whose sample commands had not been
   submitted. Before the sink-routing fix, the output could also skip host
   synchronization altogether.

This is a C3D output-routing and temporal-batch ownership bug. The minimal
Vulkan surface readback succeeds; the failure does not indicate a generally
broken Vulkan renderer.

## Fix

- Native-surface retention is now enabled only for an encoding sink. A raw
  pixel-truth sink requests host pixels.
- Vulkan temporal rendering now allocates a job-persistent intermediate sample
  surface whenever native surfaces are available, independent of whether the
  final sink is NVENC. The temporal compositor owns each sample batch through
  its weighted add, closes it, then reads back the completed accumulator once
  for a host sink.
- The canonical readback waits for its transfer submission before CPU access
  and requests the `HostReadback` memory class.

## Verification

| Case | Raw result | SHA-256 |
|---|---|---|
| Minimal Vulkan solid-red surface, 32×24; exact RGBA checked | PASS; test checks every pixel | See [`vulkan-red-surface-ctest.log`](vulkan-red-surface-ctest.log) |
| T16, no shutter | 8,294,400 bytes; 8,294,267 non-zero; range 0–255 | `66b2645e72c79a29492a07e8fed66b934d95fba293b3aaee52c44edac7717804` |
| T16, 2 shutter samples (schema minimum) | 8,294,400 bytes; non-zero output | `ea5b2dc125251fc0a29e6e0572dfb8d0b1610e5ac00f64d163e630ac91f569fe` |
| T16, 8 shutter samples, Vulkan validation + synchronization validation | 8,294,400 bytes; 8,294,270 non-zero; alpha 255 | `0416301a412fba503d97b5905dee8a872cfae9e9ab0cbc6be2e30fd568fd6ccf` |

The eight-sample raw RGB was compared to the exact center image used for the
prior parity gate: software raw frame index 4 of the nine-frame reference has
MAE 0.143, p99 3, max 82; decoded NVENC RGB has MAE 1.414, p99 4, max 29.
The existing strict-native NVENC-versus-software shutter gate remains MAE
1.457, p99 5. The corrected raw render and timing sidecar are stored beside
this file. The lossless preview [`t16-shutter-center75-vulkan.webp`](t16-shutter-center75-vulkan.webp)
was visually inspected: portrait, title, and background are present.

The minimal red-surface test passed with `VK_LAYER_KHRONOS_validation` and
synchronization validation enabled. The eight-sample render had no
`SYNC-HAZARD` or readback-copy validation message. The layer did report four
`UNASSIGNED-CoreValidation-DrawState-InvalidImageLayout` messages on
`SurfaceUploadSlot` submissions for CUDA-exportable images (expected layout
`GENERAL`, observed `UNDEFINED`); these are recorded in
`vulkan-validation-render.log` and are outside the readback command/copy. They
remain a separate Vulkan validation issue and must not be represented as a
clean full-render validation run.

The schema rejects a one-sample temporal plan (`samples` minimum is 2), so the
smallest authored temporal case is two samples. Repeated 8-sample renders with
and without validation produced identical raw SHA-256 values.
