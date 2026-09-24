# TICKET-VULKAN-ANIMATED-IMAGE-25D-RESIDENCY — preserve animated image textures

**Status**: `OPEN` — Vulkan pixel regression fixed and hardware probe passes; strict capability routing remains open
**Priority**: `P1`
**Area**: Vulkan video render path / 2.5D image layer residency

## Problem

An image layer with animated 2.5D transform tracks (`position_z`, `rotation_x/y/z`, or
`scale_z`) renders its decoded texture correctly in the static render-plan path and
in the software video backend. In the Vulkan video path, the same image can become
a black rectangle once its transform is animated. Camera-only 2.5D canary plans
render correctly, isolating the failure to animated image-layer surface/texture
residency or its per-frame materialization.

This produces a plausible, successful MP4 with missing user content, so callers
must not treat process success or frame count as visual correctness.

## Reproduction contract

Use the same image asset, 1920x1080 canvas, 30 fps, 150 frames, and layer tracks in
both paths. Render once with `--backend software` and once with Vulkan plus
`--gpu-hot-path-mode require_gpu_native`; sample frame 75. Software must contain
the image pixels; Vulkan currently returns the background color where the image
should be. Keep camera tracks out of the minimal repro. Also retain a camera-only
2.5D image canary as a control.

## Investigation scope

Trace the animated image from decoded asset through prepared/native surface
residency, transform node input binding, per-frame surface resolution, and Vulkan
composite. Check whether animation changes the surface identity or invalidates an
upload/materialized image without scheduling a fresh upload. Keep the software
path and static Vulkan image as controls. Do not hide the failure by substituting
a placeholder texture or silently falling back when `require_gpu_native` was
requested.

## Acceptance

- [ ] Animated 2.5D image texture remains resident and visible in Vulkan video
      output for the complete clip.
- [ ] A regression test compares content pixels for static and animated image
      plans on software and Vulkan, including a mid-animation frame.
- [ ] Camera-only and image-layer-only animation controls both pass.
- [ ] `require_gpu_native` either renders the correct image or rejects the
      unsupported image-motion combination before encoding; it never publishes
      a successful black-texture artifact.
- [ ] Verify on the Vulkan-enabled build and hardware lane; record plan, asset
      SHA, backend, sampled frame, and pixel-probe result.

## Delivery guard verified 2026-09-23

RenderingGen's strict `require_gpu_native` request validator rejects plans with
animated 2.5D image layers (`position_z`, `rotation_x/y/z`, or `scale_z`) before
launching the CLI. The guard has a focused test and the relevant Go packages pass uncached. A
direct CLI preflight also rejects this combination before opening video
output. On 2026-09-23, a 150-frame image-only `position_z` plan was rejected by
the Vulkan `require_gpu_native` route with exit 1 and no MP4 created. This is a
delivery guard, not a residency fix.

## Hardware reproduction 2026-09-23

Added `tests/fixtures/animated_image_residency/` with static, animated
`position_z`, and camera-only plans, a bright striped image, and
`run_pixel_probe.sh`. The asset SHA-256 is
`4fd74d76c27af19c46b1e835eab508132302eea9fa6c14f2d5ed7f1c8225bff8`; the
animated plan SHA-256 is
`b1dc823782d33ecf9da32bf7f8c40cc3cb7a6fe8dc14fc471b52c5afb00e510c`.
The initial probe ran on an NVIDIA RTX A4000 and extracted source frame 75.
Static Vulkan returned `[16, 21, 38]` (background); that failure was fixed in
the subsequent iteration below.

## Follow-up hardware verification 2026-09-23

On the same RTX A4000, the release probe was expanded to static, animated, and
camera-only plans at source frames 75 and 149, on software and Vulkan (12
renders total). Static and camera-only controls returned center RGB
`[248, 233, 39]` on both backends at both frames. Animated software returned
`[248, 233, 39]` at both frames. Animated Vulkan returned `[23, 112, 243]` at
frame 75 and `[5, 118, 208]` at frame 149. Both fail the center-pixel
acceptance (maximum channel error 225 and 243); software-vs-Vulkan full-frame
maximum channel delta was 255 for both animated frames. Static full-frame
deltas were 0; camera-only maximum deltas were 32 and 30 respectively.
The asset SHA-512 is
`e5fa4af38769d1141bb1a21710619cc29cfadd5a2c74089738b5d1a4e51bc4130ec62e2bdbcd51a9af93b968cb884062b37fbf819ca8ad21c2b9bf245652dd0d`.

The no-camera `enable_3d` plan does get a camera: `render_plan_compiler.cpp`
creates the default 2.5D camera whenever any layer has `enable_3d` and the plan
does not define a camera. `scene_context_setup.cpp` then sets
`has_camera_2_5d=true`; the graph takes the projected TransformNode path. This
rules out the assumption that `enable_3d` alone leaves `has_camera_2_5d` false.
Static and explicit camera-only controls pass on the current Vulkan run, while
the animated image still fails, narrowing the remaining defect to the moving
image's prepared/projected surface or its transform output.

The strict CLI route was also exercised with
`--gpu-hot-path-mode require_gpu_native`: an animated `position_z` plan exits
1 with `refusing require_gpu_native: animated 2.5D image texture residency is
unsupported (stage:position_z)` before encoder setup or MP4 creation. A static
plan on this host also exits 1 before encoder setup with
`required_gpu_capability_unavailable`; the permissive lane reports
`native_surface=false` at the encoder feed. No artifact is created. This is
consistent with this build/runtime lacking the Vulkan-to-CUDA native video
surface interop capability, but it means the strict static-render acceptance
is not met on this host.

The instrumented frame-75 transform confirmed an opaque native input
(`in_ink=100%`, center RGBA approximately `[0.956, 0.831, 0.021, 1]`),
`opacity=1`, valid in-bounds projective mapping parameters, and zero alpha
coverage at the transform output. The failure was downstream of that input:
the projected transform produced a tight surface with a non-zero origin, and
the following DepthGrade pass-through lost that origin when copying the
framebuffer. Composite then selected its equal-size native fast path, which
does not apply the affine origin mapping. The fix preserves origin metadata
for native surfaces through DepthGrade and permits the equal-size fast path
only when source and destination origins also match; mismatched origins use
the affine path. Strict transform input promotion now performs a synchronous
upload when the same frame consumes the promoted surface.

## Hardware verification after origin/mapping fix — 2026-09-23

On the RTX A4000, `run_pixel_probe.sh` passed all 12 renders (static,
animated `position_z`, and camera-only; software and Vulkan; frames 75 and
149). Every center pixel was `[248, 233, 39]` with max error 0. Full-frame
software/Vulkan maximum channel deltas were 0/0 for static, 25/30 for
animated, and 32/30 for camera-only at frames 75/149 respectively. The probe
also ffprobes each output. The permanent Vulkan affine projective regression
test passes (10 assertions), covering the clipped dispatch and canvas-center
sample used by the animated transform.

The strict capability item is still open. Animated `require_gpu_native` is
rejected before encoder setup by the existing unsupported-motion guard. On
this host, static strict routing resolves no Vulkan-to-CUDA native-surface
feed (`native_surface=false`) and rejects before creating an artifact. An
experimental VulkanGraph+pipe route reached the encoder but crashed because
the pipe conversion received a GPU-only framebuffer with no CPU readback;
that experimental route was removed. Do not mark this ticket FIXED until
strict static capability is correctly resolved (or explicitly rejected as
unsupported before mux) and the supported-plan acceptance is met. Temporary
diagnostic code was removed.

The investigation also found and corrected an independent affine shader ABI
mismatch: C++ stores `bilinear` as a `uint32_t`, while GLSL had interpreted the
same four bytes as a float in a `vec4 limits`; the float comparison always
selected nearest sampling. GLSL now declares the matching scalar fields, and a
Vulkan-gated boundary-sampling regression passes. Combined with origin
preservation and affine-path selection, the complete animated pixel probe now
passes at both sampled frames.
