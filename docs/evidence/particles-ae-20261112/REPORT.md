# AE-grade particles — evidence report (2026-11-12)

**Status:** local implementation and demos complete. The parent ticket remains
**PARTIAL** because same-SHA certification and the full byte-parity matrix are
not closed.

## Host

- GPU: NVIDIA RTX A4000, 16,376 MiB, driver `595.99.02`
- Vulkan ICD used for the focused run:
  `/usr/share/vulkan/icd.d/nvidia_icd.json`
- Chronon3d commit: `f126967f407b2f597205bd8e1bc7e1b2a1aae3a7`
- Worktree: dirty (the pre-existing selector work and this particle work).
  This is **not** a same-SHA certification.

## Contract implemented

- V3 neutral-default fields: `air_drag`, `emissive`, `emitter_velocity`,
  `size_over_life`, `opacity_over_life`.
- CPU sampler and Vulkan emitter share the same semantics: sub-frame emitter
  motion, quadratic drag, 256-entry over-life size/opacity LUT, 3D curl field
  with depth/parallax, HDR emissive cores, energy-conserving bokeh.
- Decoder, schema, fingerprint and V2 projection updated; legacy plans retain
  their previous fingerprint bytes.
- Vulkan uniform: 8,336 bytes, `color_lut` at offset 144,
  `over_life_lut` at offset 4240.

## Test results

| Gate | Result | Log |
|---|---|---|
| `chronon3d_particle_sampling_tests` | **PASS** — 10/10 cases, 4991/4991 assertions | `particle-sampling.log` |
| `chronon3d_c_abi_tests --test-case='*particle*'` | **PASS** — 11/11 cases, 159/159 assertions | `c-abi-particles.log` |
| `chronon3d_backend_registry_tests` build | **PASS** — exit 0, 15/15 targets | `registry-build.log` |
| `Vulkan *particle*` on RTX A4000 | **PASS** — 5/5 cases, 62/62 assertions | `registry-particle.log` |

Command for the focused GPU gate:

```bash
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json \
  ./build/chronon/linux-video-release/tests/chronon3d_backend_registry_tests \
  --test-case='Vulkan *particle*'
```

The 5 passing cases include the CPU-sampler oracle, flow/twinkle, procedural
discs, motion trails, and sprite affine/tint/opacity.

## Demo campaign

Four clips, all 1920×1080, 24/1 fps, 240 frames, 10.000 s, `yuv420p`, encoded
NVENC through the native backend:

| Clip | SHA-256 (first 16) |
|---|---|
| `01_ember_column_1080p.mp4` | `e84c76146263f383` |
| `02_smoke_curl_1080p.mp4` | `e685efb394f0f7af` |
| `03_bokeh_dust_1080p.mp4` | `e832f9da08296815` |
| `04_spark_burst_1080p.mp4` | `d9f64ff6c1422b87` |

Drive folder (36 files: clips, timing JSON, plans, logs, 16 stills, contact
sheet, generator, README, manifest):

<https://drive.google.com/drive/folders/1WlJXchsqlrdchhTUWdOiZbOq4EoFtHCo>

Every remote file was read back through the Drive API and matched on name,
byte size and SHA-256: **36/36 verified, 0 errors** (`DRIVE-FILES.json`,
`UPLOAD-MANIFEST.json`).

## Authoring findings

1. **Quadratic drag freezes demo-scale particles.** The shader uses
   `v / (1 + drag * |v| * t)`. With `drag` 0.35–2.2 and velocities of
   100–900 px/s, displacement collapses to a few pixels, so the first cuts
   showed static bands. The demo plans use `air_drag: 0.0`; drag remains
   covered by the sampler tests.
2. **Full-screen bloom as an adjustment layer produced an all-white frame**
   (YAVG 235). It was removed; the bloom adjustment is not used by these clips.
3. **Per-layer glow was clipped to the particle bounds** and produced a visible
   rectangle. It was removed; the final look relies on emissive cores,
   tone mapping and the motion-trail capsule.

## Remaining gates / not claimed as PASS

- **Same-SHA certification** on a clean host: the worktree is dirty, so this
  run cannot certify a published SHA.
- **Full byte-parity matrix** for full-budget, clipping and bokeh-depth cases:
  the focused `Vulkan *particle*` selection passed, but it is not the full
  matrix required by the ticket.
- **T1-01** (GPU canonical shadow), **T1-02** (GPU `VectorStroke`) and
  **T2-01** (`text_transitions` + spans/paint ranges) remain fail-closed under
  [ADR-040](../../adr/ADR-040-text-parity-product-exception.md). They were not
  implemented or declared PASS in this work.
