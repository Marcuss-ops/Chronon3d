# Baseline — Gate A: two-video shared-source decode-stall regression (2026-09-06)

Commit under test: `6d6d9d5a6` ("Fix native video rendering pipeline") — the
retained-frame window that replays duplicate/regressed sample requests without
a keyframe seek + GOP re-decode (two layers sharing one source path).

Raw evidence: `test_renders/two_video_streams/` (gitignored scratch — see the
JSON there for full per-run data); this file is the committed summary.

## Result: PASS

Two video layers sharing one source path render at **~103 fps** through the
warm GPU daemon. The historical **6–8 fps** stall band (per-request keyframe
GOP re-decode) is gone.

| Scenario | runs (fps) | median fps |
| --- | --- | --- |
| control: single video layer | 71.55 / 102 / 104.8 | 102 |
| **two video layers, shared source** | 104.96 / 102.65 / 103.82 | **103.8** |

Criterion: `pass_if_fps_gte = 20` (old stall band 6–8).

## Output validation (per render, via chronon receipt)

`codec=h264, 1280x720@30/1, 240 frames` — receipt `pass`, resolution / fps /
frame-count checks `pass` for both scenarios.

## Environment

- Host: YOutube, GPU NVIDIA RTX A4000, daemon IPC socket
  `/run/chronon3d/chronon.sock` (single GPU process — reproducible path on this
  host; separate-process renders stall when a second GPU process is alive).
- Binary: `build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli`
  rebuilt from HEAD.

## Deterministic regression coverage

`tests/video/test_native_video_retained_replay.cpp` (native decoder suite):
duplicate/regressed sample indices are served from the retained window without
re-decode — replay counter advances 0→1→2→2→3→3 across
decode/repeat/forward/regress sequences (green, no GPU required). Companion
fix `232dd6125` (capture-mode replay precedes the CUDA format gate) plus the
follow-up `TICKET-NATIVE-SOFTWARE-SWS-MULTIFRAME-CORRUPTION`.

## Reproduction

Plans (240 frames, 1280x720@30, assets under `test_renders/two_video_streams/assets`):

- `plan_control_single_video.json`
- `plan_two_video_shared_source.json`

Render through the warm daemon IPC runner recorded in the gate report; re-run
the deterministic replay coverage with the native decoder test binary.
