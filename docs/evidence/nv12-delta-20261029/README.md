# NV12 temporal delta export acceptance — 2026-10-29

## Build and serializer check

- `chronon3d_cli` linked from the current worktree and exported the updated
  timing sidecar fields.
- `chronon3d_cli_tests --test-case='A5 sidecar:*'`: 5/5 cases and 107/107
  assertions passed.
- Earlier full `chronon3d_cli_tests`: 273/273 cases and 5,563/5,563 assertions
  passed before the final `--no-dirty-rects` plumbing change.
- Rebuilding the current fast-dev CLI test target after that change compiled
  all touched translation units but failed while relinking the 2.2 GB static
  render archive (`ar: No space left on device`); this run is not counted as a
  pass. The current Release CLI was built and both acceptance exports below
  were run from it.

The initial strict Vulkan/NVENC probes decoded to the requested frame counts,
but their accumulated damage correctly widened to full-frame conversion. A
later 72-frame, 40×40 moving-shape fixture kept damage below the policy
threshold while crossing the 34-slot encoder ring, exercising physical-buffer
continuity and partial handoff.

| Initial probe | Frames | Full-frame Y+UV bytes | Measured Y+UV bytes | Partial handoff frames | Saved |
|---|---:|---:|---:|---:|---:|
| Static title drift | 72 | 223,948,800 | 223,948,800 | 0 | 0 B (0%) |
| Moving 240×180 rect | 40 | 124,416,000 | 124,416,000 | 0 | 0 B (0%) |

The initial probes validate conservative fallback behavior only. During
reference creation, `render --plan --no-dirty-rects` was found not to propagate
the flag into plan preparation; that plumbing is now fixed and Release-built.
The acceptance below confirms the flag disables dirty regions and produces a
full-frame reference.

| Acceptance fixture | Frames | Full-frame Y+UV bytes | Measured Y+UV bytes | Partial handoff frames | Saved |
|---|---:|---:|---:|---:|---:|
| Moving 40×40 shape, partial | 72 | 223,948,800 | 102,843,972 | 39 | 121,104,828 B (54.077%) |
| Same plan, `--no-dirty-rects` reference | 72 | 223,948,800 | 223,948,800 | 0 | 0 B (0%) |

Both strict Vulkan/NVENC exports decode all 72 frames. FFmpeg per-frame
framemd5 matches exactly (0 differing frames), and the MP4 SHA-256 is identical
for both outputs (`38aeeba507659036d9a2c1f275326cf6e07a4a016b34fba4e4fc9455ed3cabcc`).
The bounded-fixture parity and byte-savings gate passes. A broader documentary
and overlay corpus, performance certification, and same-SHA GPU certification
remain separate.

## Evidence

- [`static-title-delta-72f.plan.json`](static-title-delta-72f.plan.json),
  [`export-72f.log`](export-72f.log),
  [`static-title-delta-72f.mp4.timing.json`](static-title-delta-72f.mp4.timing.json),
  output SHA-256 `7ed83aeb4d45baec99a34f25d1820f1ed694af77fedc3ca458ff25d72a09eb13`.
- [`shape-delta-40f.plan.json`](shape-delta-40f.plan.json),
  [`shape-delta-40f.log`](shape-delta-40f.log),
  [`shape-delta-40f.mp4.timing.json`](shape-delta-40f.mp4.timing.json).
- [`shape-only-delta-40f.plan.json`](shape-only-delta-40f.plan.json),
  [`shape-only-delta-40f.log`](shape-only-delta-40f.log),
  [`shape-only-delta-40f.mp4.timing.json`](shape-only-delta-40f.mp4.timing.json),
  output SHA-256 `e9de41494fea64826c6cb4dbd75d392ab9a168d6d6846c68b95a9ba427821880`.
- [`tiny-shape-delta-72f.plan.json`](tiny-shape-delta-72f.plan.json),
  [`tiny-shape-delta-72f-partial-fixed.mp4.timing.json`](tiny-shape-delta-72f-partial-fixed.mp4.timing.json),
  [`tiny-shape-delta-72f-full-fixed.mp4.timing.json`](tiny-shape-delta-72f-full-fixed.mp4.timing.json),
  [`tiny-shape-delta-72f-fixed-comparison.json`](tiny-shape-delta-72f-fixed-comparison.json),
  and both matching MP4 outputs/logs document the parity run.
- [`cli-tests.log`](cli-tests.log).

The first 24-frame title probe was too short to reuse a 34-slot encoder buffer;
it is retained as a diagnostic record and is not used for the table above.
