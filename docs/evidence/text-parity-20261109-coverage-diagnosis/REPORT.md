# Targeted coverage investigations

## Exact 3D projected-scale candidate — rejected

A shader candidate changed the SDF-to-screen scale to include the inverse 3D X/Y rotation basis. The strict Vulkan rerenders retained the required `route=mtsdf` telemetry, but the complete output raw was byte-identical to the High range40 baseline:

- Baseline and candidate `exact_3d/vulkan.raw` SHA-256: `a39c76e58845b97ea375511df8141b3babbc215ea118ed91d71486598826d2bd`.
- Candidate one-frame metrics at frame 45 and 90 exactly matched the baseline.

The path under investigation did not change any output pixels for this RenderPlan fixture, so the candidate was removed. This rules out that shader branch as the active source of this fixture's mask error; it does not establish whether the sampled transform data reaches that branch for other text inputs.

## 2x2 subpixel coverage — rejected

See [`coverage2x/REPORT.md`](../text-parity-20261109-coverage2x/REPORT.md). Averaging four distance-field coverage samples worsened Latin IoU and over-covered the mask; the helper was removed.

## Next investigation

The Latin mismatch remains concentrated at glyph contours after global-shift checks. The next implementation target is a native coverage payload that follows the same unhinted glyph outlines and size-specific raster geometry as the software reference, while retaining distance data for GPU effects. That requires coordinated atlas, cache-key, shader, and DirectYUV review; simply changing the scalar SDF transfer has not been supported by evidence.
