# TICKET-CLI-STRICT-GPU-NATIVE-MAPPING — derive canonical strict parameters from `require_gpu_native`

**Status**: `CODE LANDED` — derivation, parity and explicit-override confirmed locally; same-SHA GPU-host verification of the strict lane still open
**Priority**: `P2` (non-blocking; contract completeness)
**Area**: CLI render/video job normalization (Phase 9 CLI/IPC parity)

## Resolution — landed in `eae160fe7`

The "one intent, two spellings" defect is fixed. The CLI entry mapping
(`apps/chronon3d_cli/commands/render/register_render_commands.cpp`) now derives
the semantic requirements from the hot-path selection itself:

```cpp
const bool strict_gpu_hot_path =
    render_args.gpu_hot_path_mode == GpuHotPathMode::RequireGpuNative ||
    render_args.gpu_hot_path_mode == GpuHotPathMode::RequireDirectYuv;
if (explicit_flag("--hardware") || !strict_gpu_hot_path)
    request.output.hardware_encoder = render_args.video_settings.hardware_encoder;
if (explicit_flag("--encoder-backend") || !strict_gpu_hot_path)
    request.output.encoder_backend = render_args.video_settings.encoder_backend;
...
request.requirements.gpu_required =
    hardware_nvenc_requested(...) || strict_gpu_hot_path;
request.requirements.cpu_fallback_allowed = !strict_gpu_hot_path;
```

Consequences that close the original problem:

- `--gpu-hot-path-mode require_gpu_native` **alone** sets
  `gpu_required=true` and `cpu_fallback_allowed=false`, and leaves
  `hardware_encoder` / `encoder_backend` empty when the caller did not pass
  `--hardware` / `--encoder-backend`.
- `resolve_canonical_execution_parameters`
  (`src/media/video/video_execution_resolver.cpp`) then derives the canonical
  strict lane from that shape (`gpu_required=true` + `cpu_fallback_allowed=false`
  → `hardware_encoder=nvenc`, `encoder_backend=native`,
  `gpu_hot_path_mode=require_gpu_native` for a composition). No second mapping
  table was introduced; the resolver remains the single authority.
- An **explicit** `--hardware` / `--encoder-backend` still travels and wins over
  the derivation.
- CLI and IPC keep one typed dispatcher over `normalize_render_job`; the IPC
  wire vocabulary (`execution_requirements`) is unchanged.

The historical `Problem` text (bare strict kept `cpu_fallback_allowed = true`,
normalized onto the pipe transport and fail-closed) described the pre-`eae160fe7`
behaviour and no longer applies.

## Evidence — local (this checkout)

`tests/cli/test_render_job_planning.cpp`:

- `strict hot-path derives the canonical native lane unless explicit` — bare
  `require_gpu_native` derives the canonical native lane; an explicit
  `encoder_backend = "pipe"` override still wins.
- `CLI and IPC boundary map identical semantics to one normalized job` — the
  CLI↔IPC parity expectations for the strict axis were updated deliberately in
  the same change.
- `cli_request_for()` mirrors the CLI mapping (`cpu_fallback_allowed =
  !strict_hot_path`) so the parity fixture cannot silently drift from the entry
  point.

Suite: `chronon3d_cli_tests` **82/82 cases, 1084/1084 assertions** on the
`linux-fast-dev` tree (2026-09-24).

The former hardware evidence (the 2026-09-24 runs recorded in the now-purged
`TICKET-VULKAN-ANIMATED-IMAGE-25D-RESIDENCY`) shows the **pre-fix** bare-strict
fail-closed, so it is no longer valid evidence for the derived behaviour and is
superseded by the checks above.

## Remaining — same-SHA GPU-host verification

### Current local CPU re-run — 2026-10-11

The current `linux-video-test` binary passed both strict mapping cases:
`strict hot-path derives the canonical native lane unless explicit` and
`CLI and IPC boundary map identical semantics to one normalized job` (**2/2
cases, 297/297 assertions**). This verifies parameter derivation, explicit
override, and CLI/IPC normalization on CPU; it does not exercise native GPU
execution or no-feed fail-closed behavior, so the two host checks below remain
open.

Only the two on-device items cannot be checked in this checkout (no native
video feed here):

- [ ] Bare `--gpu-hot-path-mode require_gpu_native` renders through
      `execution_path=full_graph_native` / `surface_handoff_path=direct` with
      `gpu_native_surface_frames > 0` and `cpu_pixel_readback_bytes == 0` on a
      capable host (same telemetry shape as `tools/verify_native_image_video.sh`).
- [ ] The same bare request fail-closes with
      `required_gpu_capability_unavailable` before setup on a host without the
      native feed.

## Acceptance

- [x] Explicit encoder flags still override the derivation, in both CLI and
      IPC normalization (`strict hot-path derives ... unless explicit`).
- [x] CLI/IPC parity expectations updated deliberately and green; no second
      parameter/mapping authority introduced (`chronon3d_cli_tests` 82/82).
- [ ] Bare strict → `full_graph_native`, zero readback (capable host).
- [ ] Bare strict → `required_gpu_capability_unavailable`, no artifact
      (incapable host).

## Close rule

Close when the two host items are recorded on the same SHA (either as PASS on a
capable host, or as the explicit fail-closed rejection on an incapable one); the
scheda then becomes purge-eligible like the other terminal tickets.
