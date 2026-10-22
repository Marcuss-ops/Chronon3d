# T16 shutter pixel parity — 2026-10-22

Focused frame-75 A/B at 1920×1080, using the same T16 three-depth RenderPlan
assets and resolved center camera sample. The shutter plan uses 90° and eight
samples. The GPU render ran on RTX A4000 through Vulkan's strict native graph
and H.264 NVENC; the reference is the center frame from a nine-frame software
render. Both GPU MP4 and software RGBA reference were decoded/measured by
`measure.py`.

Result: decoded GPU shutter versus software shutter is RGB MAE **1.457** and
p99 absolute error **5** (declared gate: MAE ≤ 2, p99 ≤ 5). The GPU shutter
changes 16.18% of RGB channels relative to the matched GPU no-shutter NVENC
render. The software shutter changes 4.00% relative to its matched software
baseline. This closes the focused center-frame shutter parity gate; it does not
claim all-frame or moving-object certification.

The pre-encode raw Vulkan shutter sink was also tried, but returned an all-zero
RGBA frame. That exposed a separate readback synchronization defect when a
temporal accumulator is nested in an active frame batch. It is retained as a
known failure and is not used for the PASS above. The strict-native NVENC path
does produce a valid image; the batch/readback fix and its follow-up raw check
are recorded in the C3D ticket.

Reproduction from the repository root:

```sh
Chronon3d/build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli render \
  --plan Chronon3d/docs/evidence/camera-authority-migration/T16/shutter-parity-20261022/t16-shutter-center75.plan.json \
  --backend vulkan --gpu-hot-path-mode require_gpu_native \
  --video-sink ffmpeg --hardware nvenc --codec h264_nvenc \
  -o /tmp/t16-shutter-frame75.mp4 \
  --assets-root "$PWD/Chronon3d" -v info
python3 Chronon3d/docs/evidence/camera-authority-migration/T16/shutter-parity-20261022/measure.py
```

`metrics.json` records the exact measured values. `t16-no-shutter-center75.plan.json`
is the matched ablation. The nine-frame software raw files, decoded RGB outputs,
MP4s and timing sidecars are included to reproduce the comparison.
