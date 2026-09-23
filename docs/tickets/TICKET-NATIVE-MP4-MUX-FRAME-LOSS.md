# TICKET — Native MP4 mux drops encoded frames

**Status**: IN PROGRESS — native flush loss fixed; cross-backend matrix and default policy pending  
**Priority**: P1 — silent output corruption  
**Area**: CLI video export / native encoder backend

## Evidence

On 2026-09-23, the 137-frame, 30 fps plan `motion_bear_reference_10_thank_you_typewriter`
was rendered through the software backend. The native encoder logged 137 frames
written and returned success, but `ffprobe` and the generated receipt reported
only 86 MP4 frames (2.8667 s). The CLI warning marked the artifact not
copy-eligible, but the render command still exited successfully.

The same plan exported with `--encoder-backend pipe` produced 137/137 frames at
30 fps. The suite now uses the pipe backend uniformly until the native mux path
is understood; it gates every artifact on exact fps and frame count.

## Required diagnosis and fix

- Reproduce in a focused C++/CLI regression fixture, including native and pipe
  encoder backends.
- Inspect submitted frame PTS, encoder packet PTS/DTS, packet drain, and mux
  trailer accounting to identify why the native path exposes 86 packets from
  137 submitted frames.
- Add a regression test that fails on packet/frame loss and covers durations
  around the observed boundary, not only this one fixture length.
- Make a successful render impossible when final media verification finds a
  frame-count mismatch. Return a non-zero status and preserve the receipt/log
  diagnostic.
- Restore the native backend as default only after the regression and the
  affected export gates pass.

## Mitigation landed 2026-09-23

The shared output contract now treats any known frame-count mismatch as a
render verification failure (previously `passed=true` with only
`copy_eligible=false`), and the regression suite includes a deliberately
truncated three-frame MP4 against a four-frame contract. On 2026-09-23, the
exact 137-frame CLI fixture reproduced the loss: the CLI accepted 137 frames,
libx264 emitted 86 packets with PTS 0..85, the mux received 86, verification
reported `got 86, expected 137`, and the CLI exited 1. The cause was the
native CLI wrapper ignoring `avcodec_send_frame(codec, nullptr)` returning
`EAGAIN`: it drained once but never retried the flush, so buffered frames were
left out. Close now drains and retries before finalizing.

The same 137-frame fixture now passes on native and pipe: both report
137/137 at 30 fps, exit 0, and have successful copy-eligible verification.
The native sink packet test passes for 60, 86, 87, 96, 120, 137, 150, 180,
and 300 frames. A permanent CLI-wrapper test exercises those same counts at
1920x1080 and passes (1 test case, 1280 assertions). The exact 137-frame
fixture passes through both CLI backends, but the complete duration matrix has
not yet been run through the pipe CLI backend, and the suite's pipe-default
workaround remains in place pending that parity check.

## Acceptance

- Native and pipe exports have identical expected frame count, frame rate, and
  decodability for the regression matrix.
- A deliberately truncated/mismatched artifact causes a non-zero CLI result.
- No frame-count mismatch can produce a success receipt eligible for delivery.
