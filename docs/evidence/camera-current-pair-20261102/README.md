# Camera acceptance pair — re-pin on the current revisions (2026-11-02)

Re-run of the frozen software-vs-Vulkan raw RGBA8 sentinel oracle on the
**current** Chronon3D / ChrononMotion3D revisions, because the PASS recorded in
`../camera-closure-final/parity-v3-raw-sentinels.json` belongs to the older pair
C3D `9b5d2383` / CM3D `b778afc8` (both dirty), which is now 59 resp. 13 commits
behind the submodule heads. Nothing in the oracle was changed: the same plan,
the same sentinel frames (30/75/149), the same preregistered ROIs and the same
frozen thresholds (MAE ≤ 2.0, p99 ≤ 10) are reused from
`measurement-contract.json`, copied verbatim from `camera-closure-final/`.

## Pin

`revisions.json` records the pinned state:

| Repo | HEAD | Dirty |
|---|---|---|
| Chronon3d | `9fc4b70031edb2c844b05f6fd989339c4b2251c6` | yes (tracked diff `074c5f19…`, 38 untracked) |
| ChrononMotion3D | `5b9aa974dbdb1b011cf008d9a19e9168f9e05aa3` | yes (tracked diff `ac463fcc…`) |

`tracked_diff_sha256` is `sha256(git diff)` of tracked changes at capture time.

The measured commit is `9fc4b700`; two commits landed afterwards
(`0bbdefd78`, `8d34ae9dd`) that touch only `docs/evidence/real_optical_assets`
(plan JSON, bundled font, README, `.gitignore`) — no `src/`, `include/`,
`tests/` or `apps/` change, so the measured binary describes the current HEAD
as well. The working tree has meanwhile diverged further (other uncommitted
work), which is why this stays local dirty-tree evidence.
The CLI binary used for the render is
`Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli`,
SHA-256 `a7b778acb712ff6cfe1297e7ed79a49a5e08d1271740de3e5af669dc09c2c5a1`
(Release, `ninja chronon3d_cli -j6` after the two fixes listed below).

## Commands

From `Chronon3d/`:

```bash
ninja -C build/chronon/linux-video-release chronon3d_cli -j6

PLAN=docs/evidence/camera-closure-final/optical_current_pair_v3.plan.json
OUT=docs/evidence/camera-current-pair-20261102

for f in 30 75 149; do
  CHRONON3D_VULKAN_VALIDATION=1 \
  build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli render \
    --plan "$PLAN" --assets-root "$PWD" --backend software \
    --start-frame $f --end-frame $f \
    --video-sink raw --pipe-pixfmt rgba --encoder-backend pipe \
    -o "$OUT/parity-v3-software-$(printf %03d $f).raw.mp4"
done

for f in 30 75 149; do
  CHRONON3D_VULKAN_VALIDATION=1 \
  build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli render \
    --plan "$PLAN" --assets-root "$PWD" --backend vulkan \
    --start-frame $f --end-frame $f \
    --video-sink raw --pipe-pixfmt rgba --encoder-backend pipe \
    -o "$OUT/parity-v3-vulkan-$(printf %03d $f).raw.rgba" \
    > "$OUT/parity-v3-vulkan-$(printf %03d $f)-raw.log" 2>&1
done

python3 docs/evidence/camera-current-pair-20261102/measure_parity_raw.py
```

`--gpu-hot-path-mode require_gpu_native` is **not** usable with `--video-sink
raw`: the writer rejects the raw sink as "non-GPU surface" (fail-closed) and the
run exits 1 without output. That is the documented contract, not a regression;
the raw oracle is the non-strict readback lane.

## Result — PASS under the frozen thresholds

| Frame | RGB MAE | p99 | max | Vulkan validation errors | Gate |
|---|---:|---:|---:|---:|---|
| 30 | 0.1740 | 1 | 5 | 0 | PASS |
| 75 | 0.1772 | 2 | 27 | 0 | PASS |
| 149 | 0.1523 | 1 | 2 | 0 | PASS |

Aggregate gate: **PASS** (`parity-raw-sentinels.json`). Plan SHA-256
`c2467b44fed0f0d007af7b3e38a3dd57e13728295b506701d7d3c02306e376d6`, i.e. the
same plan file as the previous pair.

The software raw frames are **byte-identical** to the previous pair
(`4ab2033a…` frame 30, `3b2019bf…` frame 75), so the software lane did not move
across the 59 commits; the metrics reproduce to four decimals. That makes the
new numbers a re-pin rather than a new measurement with a new baseline.

## What this certifies

- The frozen sentinel parity gate C5 of
  [`TICKET-CAMERA-DEFINITIVE-CLOSURE`](../../tickets/TICKET-CAMERA-DEFINITIVE-CLOSURE.md)
  §8.3 reproduces on the current pair, with validation enabled and zero
  validation errors.
- The two defects fixed in this session did not move the parity:
  `tests/runtime/test_prepared_render_job_allocations.cpp` (global `operator
  new/delete` replacement returned an unannotated block for alignments ≤
  `max_align_t`, so the aligned delete read glibc heap metadata as a pointer and
  crashed in `Scene::~Scene`) and
  `src/render_graph/framebuffer_acquire_owned_detail.hpp` (the planned-slot
  passthrough probed storage through the mutable `data()`, which clears
  `content_cleared`, after propagating the flag).

## What this does NOT certify

- **Not same-SHA**: both trees are dirty; `revisions.json` pins the diff digest,
  not a reviewed commit. CI on an exact SHA remains `BLOCKED`.
- Not the visual alpha-edge acceptance of §8.4, not the human whole-video review,
  not the 150-frame 1080p pair re-run (only the three sentinels), and not the
  Motion provider equivalence (unchanged, measured on the previous pair in
  `../camera-closure-final/motion-provider-full-scene-parity.json`).
