# GEL reference match — 2026-11-06

The Drive target is preserved as `drive-reference.png` (1094×143, SHA-256
`179aea0bdc26e23cae844e2381232d836f53e261a5eb429060d84e148393a959`). The
current render plan uses the evidence-local Liberation Mono Bold font, three
separately positioned word layers, the extracted background plate, the C² GEL
dome, the SDF edge coat, a cyan/mint caustic lobe, and two glow passes. The font
binary and license notice are documented in
[`../gel-reference-camera-20261106/STATIC-REFERENCE-MATCH.md`](../gel-reference-camera-20261106/STATIC-REFERENCE-MATCH.md).

The current raw is a strict Vulkan native hot-path capture. SHA-256:

- RGBA: `8756684a422fb1d89cab1ce44ec0ce0d2d3235e5e49c9f63af5b4c9174761b60`
- PNG: `5db9359eb97b29c1b4219c5f9e5fdffe21e6b8e478c50a11f8deaeaffc4b6644`
- H.264 preview: `481c061b83bb7991970318d5aae2a8bdb83896baba709037d59715a3b000244e`

The [side-by-side comparison](reference-comparison.png) and
[`comparison-metrics.json`](comparison-metrics.json) show that horizontal word
extents are close to the target. The simple cyan-pixel counts include plate
pixels and glow; they are diagnostic only, not a text-mask or optical-match
oracle. Review still shows lower body density and weaker blue/multicolor
reflections than the supplied screenshot. `GEL-VISUAL` remains open. The
supplied Drive file is a still image, so it cannot validate moving footage.

The three-second review preview is
[`gel-reference-progress-20261106.mp4`](gel-reference-progress-20261106.mp4)
and is also on [Drive](https://drive.google.com/file/d/1lr9RpTFwjzBOqfwIkJxfk8buAP9X4tq6/view?usp=drivesdk).

Reproduce from the `Chronon3d` directory:

```sh
build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli render \
  --plan docs/evidence/gel-reference-match-20261106/reference.plan.json \
  --assets-root "$PWD" --backend vulkan --gpu-hot-path-mode require_gpu_native \
  --start-frame 0 --end-frame 0 --video-sink raw --pipe-pixfmt rgba \
  --output /tmp/gel-reference-match.rgba
ffmpeg -hide_banner -loglevel error -f rawvideo -pixel_format rgba \
  -video_size 1094x143 -i /tmp/gel-reference-match.rgba \
  -frames:v 1 -y /tmp/gel-reference-match.png
```
