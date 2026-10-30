# Camera audit 2026-10-30 — provider look-at/roll + showcase renders

## 1. Runtime provider look-at/roll — fixed

**Defect (confirmed in current source).** `makeCameraSampleProvider()` called
`lowerCameraPose(rig->sample(seconds), ...)` directly. `CameraRig::sample()`
keeps `hasTarget` and `roll` metadata after baking both into the orientation,
while `lowerCameraPose()` intentionally rejects unresolved metadata. The
offline bake path cleared those fields; the runtime provider did not, so a
camera with look-at or roll failed at runtime.

**Fix.** `ChrononMotion3D/src/chrononmotion/motion/RenderPlanLoweringCamera.cpp`:
the provider now clears `pose.hasTarget` and `pose.roll` after
`CameraRig::sample()` and before `lowerCameraPose()`. `lowerCameraPose()`
still fails closed for direct callers that pass unresolved poses.

**Test.** `chrononmotion_camera_bake_roundtrip` on
`ChrononMotion3D/build/release-fast`:

```
185 checks, 0 failures
```

The new section covers look-at + roll, offline-bake/runtime equality at frame 0,
and an exact fractional-frame sample at 15.5. It also asserts that a direct
`lowerCameraPose(rig->sample(...))` still throws. Log:
[`motion-provider-test.log`](motion-provider-test.log).

## 2. Euler interpolation

The current `Chronon3d/src/render_plan/render_plan_compiler_animation_utils.cpp`
uses `glm::slerp` on the camera quaternions plus
`math::camera_rotation_euler`, not `glm::mix` on Euler angles. The ±180°
wrap-around defect described by the audit is not present in this tree.

## 3. Showcase renders — uploaded

Three strict Vulkan/native NVENC renders, 1920×1080, 30 fps, 150 frames
(5.0 s each), H.264. Frame 0 is non-black and `blackdetect` found zero black
intervals in all three.

| File | Bytes | Frame-0 mean | SHA-256 | Drive |
|---|---:|---:|---|---|
| `camera_t16_hero_title_150f_1080p.mp4` | 2,579,945 | 61.88 | `28e3e49746a6d8390aec3217d0b582d175f293069384b1fb571e113780344d13` | [open](https://drive.google.com/file/d/1JhxPYz3nXGVIG8rHTpJWDNwOydY5UZzR/view?usp=drivesdk) |
| `camera_t16_three_depth_dof_150f_1080p.mp4` | 1,998,388 | 63.31 | `6c3e8f347adce1bc0b7547b2397316e5c5c66c8c7d055eedf11a997644dc2395` | [open](https://drive.google.com/file/d/11pLi-Zg1Of-CEKi_K4bDQlcv7SrGZgLT/view?usp=drivesdk) |
| `camera_t16_three_depth_no_dof_150f_1080p.mp4` | 2,598,342 | 63.09 | `10d37f6d8de67de360f77fa502b7c9e65d8ceca91f532ababc453977d636cb7d` | [open](https://drive.google.com/file/d/1sUa1i8CD7exn9jCwOGoJ8KlkV6UVQT-D/view?usp=drivesdk) |

Drive parent: [folder](https://drive.google.com/drive/u/1/folders/1J_xUGo_bchzXDIGqSX04CU44c_Dm3SxS)
(`1J_xUGo_bchzXDIGqSX04CU44c_Dm3SxS`). Uploaded with
`RenderingGen/renderinggen/cmd/drive-upload` (OAuth credentials/token from
`RenderingGen/UploadDrive/`); every upload reported `DRIVE_UPLOAD_PASS` with
the size and SHA-256 above.

Plans:

- [`T16 Hero Title`](../../../output/T16_Hero_Title/scene.camera.plan.json)
- [`T16 three-depth DoF`](../camera-authority-migration/T16/dof-ablation/t16_three_depth_dof_on.plan.json)
- [`T16 three-depth no-DoF`](../camera-authority-migration/T16/scene.camera.three_depth.plan.json)

The documentary showcase plan was not used: strict-native fails on
`shape_repeater_0` with `UnsupportedCapability`, before the camera/encoder. That
is an overlay/shape gap, not a camera defect.

## 4. What the audit still leaves open

- **Sub-frame camera/layer contract:** the runtime provider samples exact
  fractional frames; offline `camera_samples` are integer-frame samples and C3D
  interpolates them. Arbitrary easing is not proven bit-equivalent between the
  two paths.
- **DoF fail-closed:** `per_pixel_dof_node.cpp` can return a clone without blur
  when depth is empty; a scene that expected DoF sources is not distinguished
  from a legitimately flat scene.
- **Capability validation:** `CameraRig` accepts FOV (0, 180), the lowering
  contract accepts 10–120, and iris/highlight settings are not renderable yet.
  A shared Motion → RenderPlan capability check is still needed.
- **Vulkan validation layers:** the historical GENERAL/UNDEFINED layout errors
  must be rerun clean on the current build.
- **CI same-SHA:** Chronon3D and ChrononMotion3D are not green on the current
  SHAs; no official camera closure until that is green.

The camera is therefore **PARTIAL**: the concrete look-at/roll bug is fixed and
Euler was already correct, but the sub-frame contract, DoF fail-closed,
capability validation, Vulkan validation and same-SHA CI remain open. No third
camera or new evaluator was added.
