# TICKET-VULKAN-ANIMATED-IMAGE-25D-RESIDENCY — preserve animated image textures

**Status**: `OPEN` — strict delivery guard verified; Vulkan transform/residency defect reproduced
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
The probe ran on an NVIDIA RTX A4000 and extracted source frame 75. Both
software controls returned center RGB `[248, 233, 39]` (expected yellow,
maximum channel error 2); static Vulkan returned `[16, 21, 38]` (background,
maximum error 234), so the probe script fails as intended. Temporary transform
instrumentation also measured nonzero alpha coverage at the animated transform
input and zero coverage at its output. The visible content disappears at the
transform stage in this reproduction; a corrective Vulkan fix and passing
pixel regression are still required. The camera-only plan is included in the
fixture, but a passing Vulkan control result has not yet been recorded.

## Follow-up hardware verification 2026-09-23

On the same RTX A4000, the single-frame-at-source-frame-75 probe was rerun
against the current release CLI. Static and camera-only controls returned
center RGB `[248, 233, 39]` on both software and Vulkan. Animated software also
returned `[248, 233, 39]`; permissive Vulkan returned `[23, 112, 243]`, so the
animated image still fails the pixel contract. The asset SHA-512 is
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
plan on this host also fails closed because the resolved required GPU
capability is unavailable (`native_surface=false` at the encoder feed). This
confirms the delivery guard, but does not satisfy the residency fix or make the
permissive Vulkan render correct; status remains `OPEN`.
