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
