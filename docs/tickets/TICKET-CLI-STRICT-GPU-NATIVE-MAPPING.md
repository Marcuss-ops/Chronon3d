# TICKET-CLI-STRICT-GPU-NATIVE-MAPPING — derive canonical strict parameters from `require_gpu_native`

**Status**: `OPEN`
**Priority**: `P2` (non-blocking; contract completeness)
**Area**: CLI render/video job normalization (Phase 9 CLI/IPC parity)

## Problem

`chronon3d_cli render ... --gpu-hot-path-mode require_gpu_native` **without**
`--encoder-backend native --hardware nvenc` cannot reach the GPU-native full
graph even on a capable host. The CLI requirement normalization keeps
`cpu_fallback_allowed = true`, and the canonical parameter resolver maps that
shape to the pipe transport (`hardware_encoder=none`,
`encoder_backend=pipe`), so `cli_effective_capabilities` reports
`native_nvenc=false` and the media resolver answers
`required_gpu_capability_unavailable`; the CLI fails closed before setup with
no artifact.

The positive strict route only opens when the caller repeats the strict
intent as raw encoder flags (`--encoder-backend native --hardware nvenc`), or
when RenderingGen sets `execution_requirements` explicitly on the IPC wire
(`cpu_fallback_allowed=false`), which
`resolve_canonical_execution_parameters` already maps to nvenc + native
transport. The strict hot-path mode is therefore advisory instead of
authoritative: one intent, two spellings, and the short spelling silently
fail-closes.

Evidence: the 2026-09-24 closeout runs in
[`TICKET-VULKAN-ANIMATED-IMAGE-25D-RESIDENCY`](TICKET-VULKAN-ANIMATED-IMAGE-25D-RESIDENCY.md)
(bare strict → `required_gpu_capability_unavailable`, no artifact; strict +
native/nvenc → `required_gpu_native_full_graph`, `native_surface=true`,
`gpu_native_surface_frames=1`, `cpu_pixel_readback_bytes=0`, pixel
max_error 0).

## Required behavior

- `require_gpu_native` (and `require_direct_yuv`) alone derive the canonical
  strict parameters through the single authority
  `resolve_canonical_execution_parameters` (nvenc + native transport for a
  strict GPU request); no second mapping table, no new resolver.
- On a capable host the bare strict request resolves
  `required_gpu_native_full_graph` and feeds `native_surface=true` with zero
  CPU readback; the rendered artifact is pixel-correct.
- On an incapable host the bare strict request still fails closed with
  `required_gpu_capability_unavailable` before setup; no artifact is created.
- Explicit `--encoder-backend` / `--hardware` keep overriding the derivation.

## Constraint — Phase 9 CLI/IPC parity

The current CLI→requirements mapping is codified as expected JSON in
`tests/cli/test_render_job_planning.cpp` (e.g. `require_gpu_native`
normalizes to `"cpu_fallback_allowed":true`). Changing the derivation is a
contract change: update those parity expectations deliberately in the same
change, and keep CLI and IPC normalization identical (one typed dispatcher
over `normalize_render_job`). RenderingGen's wire behavior must not change
underneath — the daemon contract consumes the same
`execution_requirements` vocabulary.

## Acceptance

- [ ] Bare `--gpu-hot-path-mode require_gpu_native` renders through
      `execution_path=full_graph_native` / `surface_handoff_path=direct` with
      `gpu_native_surface_frames > 0` and `cpu_pixel_readback_bytes == 0` on a
      capable host (same telemetry shape as `tools/verify_native_image_video.sh`).
- [ ] The same request fail-closes with `required_gpu_capability_unavailable`
      before setup on a host without the native feed.
- [ ] Explicit encoder flags still override the derivation, in both CLI and
      IPC normalization.
- [ ] CLI/IPC parity expectations updated deliberately and green; no second
      parameter/mapping authority introduced.
