# Broadcast six-test evidence — 2026-11-08

**Status: PASS** — all six checks pass. The machine-readable result is
`report.json` (`overall: PASS`).

The evidence was rendered with the **Vulkan** backend on an NVIDIA RTX A4000
(16 GB), not with the software backend. `run_evidence.sh` defaults to Vulkan.
Renders were run one at a time: host memory stayed around 8 GB used and GPU
memory around 5.7 GB of 16 GB; no OOM occurred.

The six existing camera-ring MP4s in
`docs/evidence/camera-ring-text-drive-20261108/` are **not certified** by this
bundle. They are text-only compositions and the plan-level audit in
`report.json -> audit` marks them FAIL/N/A. The certified fixture is the
purpose-built canary described below.

## Results

| # | Check | Fixture / evidence | Result | Measured |
|---|-------|--------------------|--------|----------|
| 1 | Velocity profile | `badge` scale track, cubic-bezier(0.35, 0.7, 0.2, 1.0) | **PASS** | peak time 0.190 (limit 0.15–0.25), tail speed ratio 0.006564 (< 0.05) |
| 2 | Topological morph | `broadcast_canary_morph.svg`, 12-point star → dodecagon | **PASS** | 0 self-intersections at t=0.5, max 0, stable winding, area 36719.989 → 53279.992 → 67499.995 (monotonic) |
| 3 | 60 fps stagger + moving light centroid | `broadcast_hierarchy_60fps`, 960×540, 120 frames | **PASS** | 4 layers, starts 0/2/4/6, stagger 33.33/33.33/33.33 ms, centroid path 113.52 px (≥ 10), max step 10.16 px (≤ 24) |
| 4 | Trim whip & latch | `underline` trim, frames 2–32 | **PASS** | head/tail speed ratio 3.549 (≥ 2.5), 48 stretch samples, 212 latch samples, final start/end 1.0 |
| 5a | Sub-pixel raster, 4K | `subpixel_4k`, 3840×2160, 30 frames | **PASS** | vertical CV 0.02188 (≤ 0.04), vertical step 0.04268 (≤ 0.08), horizontal CV 0.02169 (≤ 0.04), width 4–5 px (pulse 1), seam ratio 1.0 (≥ 0.90), alpha opaque |
| 5b | Sub-pixel raster, 720p | `subpixel_720`, 720×405, 30 frames | **PASS** | vertical CV 0.02348, vertical step 0.0495, horizontal CV 0.02301, width 4–5 px, seam ratio 1.0, alpha opaque |
| 6 | Materiality | decoded 1080p canary frame | **PASS** | grain RMS 0.02306 (2.31 %, inside the brief's 1.5–2.5 % band), bevel luma delta 0.01429 (≥ 0.01), shadow 0.03347 vs reference 0.03940 (darker), corner R/B p99 31 / max 160 |

Test 1 is measured from the authored curve, test 2 from the SVG geometry, test
4 from the trim keys, and test 3/5/6 from decoded raw RGBA frames. Test 6 is a
static materiality sample; the canary is rendered at frames 60–90, where the
entrance animations have settled.

## Existing camera-ring plans: audit

All six plans (`camera_ring_text_01…06`) have the same plan-level status:

- **1_velocity_profile: FAIL** — peak at 0.0292, outside 0.15–0.25; tail speed
  ratio 1.000, not < 0.05.
- **3_hierarchy_stagger: N/A** — single-focus composition.
- **4_trim_dynamics: N/A** — no animated trim path.
- **6_materiality_static: FAIL** — no lit SDF bevel with contact shadow and no
  non-zero film grain; the monochrome variant also has no peripheral chromatic
  dispersion.

`report.json` embeds this audit. `camera_ring_audit.json` is the standalone
plan-only audit from the initial pass.

## Artifacts

- `report.json` — all metrics and the existing-plan audit.
- `broadcast_canary.plan.json`, `broadcast_canary_1080p.mp4`,
  `canary_frame_090.png` — materiality/velocity canary (1080p30).
- `broadcast_hierarchy_60fps.plan.json`, `broadcast_hierarchy_60fps.mp4`,
  `hierarchy_frame_040.png` — 60 fps stagger hierarchy (960×540).
- `broadcast_canary_morph.svg` — topology fixture.
- `subpixel_4k.plan.json`, `subpixel_4k.mp4`,
  `subpixel_4k_line_frame_015.png`, `subpixel_4k_seam_frame_015.png`.
- `subpixel_720.plan.json`, `subpixel_720.mp4`,
  `subpixel_720_line_frame_015.png`, `subpixel_720_seam_frame_015.png`.
- `logs/` — the Vulkan render logs from the final runner pass.
- `run_evidence.sh` — one-command reproduction.

The 4K/720 MP4s are H.264 presentation copies. Every metric reads the raw RGBA
frames, never the encoded video. Raw frames live in
`${TMPDIR:-/tmp}/chronon-broadcast-six` (about 2.3 GiB) and are not committed.

## Reproduce

```bash
cd Chronon3d
cmake --build build/chronon/linux-video-release \
    --target chronon3d_cli chronon3d_effects_tests -j6

./build/chronon/linux-video-release/tests/chronon3d_effects_tests \
    --test-case="SdfBevel*"      # 6/6 cases, 20/20 assertions
./build/chronon/linux-video-release/tests/chronon3d_effects_tests \
    --test-case="CameraRing*"    # 7/7 cases, 781/781 assertions

CHRONON_BACKEND=vulkan bash \
    docs/evidence/broadcast-six-test-20261108/run_evidence.sh
```

The runner renders the canary (frames 60–90), the 60 fps hierarchy (0–119), the
4K fixture (0–29) and the 720p fixture (0–29), encodes the MP4s, writes the PNG
crops and regenerates `report.json`. `CHRONON_BACKEND=software` is available
only for a CPU comparison; it is not the recorded run.

Manual gate, if the raws already exist:

```bash
python3 tools/broadcast_six_diagnostics.py \
    --plan docs/evidence/broadcast-six-test-20261108/broadcast_canary.plan.json \
    --hierarchy-plan docs/evidence/broadcast-six-test-20261108/broadcast_hierarchy_60fps.plan.json \
    --morph docs/evidence/broadcast-six-test-20261108/broadcast_canary_morph.svg \
    --canary-raw /tmp/chronon-broadcast-six/broadcast_canary_1080p.raw \
    --canary-width 1920 --canary-height 1080 \
    --hierarchy-raw /tmp/chronon-broadcast-six/broadcast_hierarchy_60fps.raw \
    --hierarchy-width 960 --hierarchy-height 540 \
    --subpixel-4k-raw /tmp/chronon-broadcast-six/subpixel_4k.raw \
    --subpixel-4k-width 3840 --subpixel-4k-height 2160 \
    --subpixel-720-raw /tmp/chronon-broadcast-six/subpixel_720.raw \
    --subpixel-720-width 720 --subpixel-720-height 405 \
    --audit-dir docs/evidence/camera-ring-text-drive-20261108 \
    --output docs/evidence/broadcast-six-test-20261108/report.json
```

## Implementation notes and limitations

- **Vulkan rasterizer is 1× sampled.** The GPU backend has no MSAA or analytic
  edge AA; 2D edges are hard. The sub-pixel check therefore measures composited
  coverage stability, not CPU-style analytic AA. The 3 px authored line
  rasterizes to 4–5 px on both resolutions, with a width pulse of 1 px (within
  the test's allowance).
- **`position` tracks are not evaluated for shape layers in this build.** The
  compiler parses them and uses them for motion-blur sampling, but regular
  layer placement ignores the animation. Test 3 therefore uses staggered
  scale-in entrances (0.01 → 1.0) instead of opacity/position entrances; the
  light centroid sweeps 113.52 px as the four layers grow. Test 5 produces its
  3 px sweep by offsetting the line inside a full-frame layer and scaling that
  layer (1 − 3/(0.24w) vertically, 1 + 3/(0.28h) horizontally). This changes
  line thickness by less than 2.7 % at 720p and less than 0.5 % at 4K.
- **Open stroked paths need an explicit transparent fill on Vulkan.** Without
  `"fill": [0, 0, 0, 0]`, the native path promotion drops the layer entirely.
  The canary underline and corner ticks include that fill; the earlier canary
  pass was missing both open-stroke elements. The corner R/B p99 rose from 7 to
  31 once the ticks rendered.
- **Test 6 grain band.** The diagnostic accepts 0.012–0.030 RMS to tolerate
  GPU/CPU texture variation; the measured 0.02306 is inside the brief's
  stricter 1.5–2.5 % band.
- **Not certified: existing camera-ring clips.** The six MP4s in
  `docs/evidence/camera-ring-text-drive-20261108/` are text-only; their plans
  fail the velocity and materiality audits. They remain useful visual evidence
  for the camera-ring effect, but they are not evidence for the six criteria.
