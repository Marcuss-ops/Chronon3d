# T16 FOV A/B probe — 2026-10-21

This is the pixel-truth CAM-01 probe. Two 1920×1080 RenderPlans derived from the three-depth T16 plan were rendered at frame 75 using `--video-sink raw`; static variants without `camera_samples` isolate camera optics from animation interpolation. The still/PNG image lane is not pixel truth: it warns that it omits text composition and does not vary by frame/backend. Earlier still-lane images are retained with `_still_lane_non_truth` in their names and are not used for conclusions.

## FOV change

The 30° and 50° raw frames visibly change framing on both backends. RGB comparison reports MAE 62.38 (software) and 62.37 (Vulkan), maximum channel differences 245/247, with 94.79%/94.79% of pixels changed. Thus CAM-01's RenderPlan FOV ablation passes on the sampled T16 frame. The reviewed contact sheet is `t16_fov_ablation_contact.png`; verbatim raw captures are `t16_camera_{30,50}_{software,vulkan}.raw.rgba`, and the PNGs alongside are direct RGBA8 projections of those raw frames.

## Cross-backend and pose checks

At the same FOV, software and Vulkan pixel-truth captures are close: MAE 0.081 at 30° and 0.131 at 50° (max 31/89; expected backend rounding). A software camera X shift of 500 units changes 94.02% of pixels (MAE 50.36) compared with the static FOV-50 capture. This confirms that camera pose and lens values reach the full RenderPlan scene path. The X-shift is a diagnostic control, not the T16 dolly acceptance gate.

The raw sink logs and source plans are retained beside the captures. Do not use `t16_fov_*_still_lane_non_truth.png` when assessing render quality; those artifacts demonstrate the known image-lane limitation that initially produced a false zero-difference result.
