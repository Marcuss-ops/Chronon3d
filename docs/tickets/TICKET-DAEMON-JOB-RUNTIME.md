# TICKET-DAEMON-JOB-RUNTIME — persistent C++ daemon authority

## Stato

PARTIAL — persistent prepared-frame runtime and additive FlatBuffers job
lifecycle protocol landed. The canonical CLI daemon owns plan-backed
whole-video `RENDER_JOB` and now exposes its lifecycle over IPC when a client
supplies `job_id`. The FlatBuffers job API currently tracks one prepared frame
and does not yet implement plan-backed whole-video execution.

## Implementato

- `DaemonRuntime` owns the persistent `RenderEngine` and `CompositionSession`.
- IPC handlers are serialized at the runtime boundary before touching GPU
  state; transport concurrency cannot create accidental concurrent engine use.
- Explicit lifecycle states: `starting`, `warming`, `ready`, `draining`,
  `poisoned`.
- Daemon warmup gate runs before the service announces readiness.
- Default Unix socket is `/run/chronon3d/chronon.sock`.
- `CompositionSession` owns a `PreparedRenderJob` per composition; prepare,
  resource warm-up and slot/pool setup happen once at create time, and frame
  calls reuse the prepared handle.

## Protocollo job aggiunto

- FlatBuffers includes `PrepareJob`, `RenderJob`, `ReleaseJob`,
  `GetJobStatus`, and `Health`, while keeping the existing composition/frame
  messages for compatibility.
- Per-session status covers prepared, running, completed, failed, and released
  states. `Health` reports daemon readiness; `RenderJob` is synchronous and
  renders one frame through the existing prepared renderer. Its framebuffer is
  not yet encoded, persisted, or delivered to the client.
- Released and prepare-failed status tombstones are bounded to the latest 256
  entries; prepared resources are not evicted by this status-history bound.
- `CompositionSession` remains the lifecycle owner; no duplicate job registry
  was introduced. The wire-contract decision is Proposed in
  `docs/adr/ADR-032-daemon-job-lifecycle-protocol.md` pending CI validation.

## Prossimo blocco

For the dedicated FlatBuffers daemon:

- Replace the placeholder composition descriptor with a plan-backed factory
  that shares the canonical CLI plan semantics.
- Route complete plans through bounded scheduler/worker and encoder ownership,
  preserving Vulkan/CUDA/NVENC resources for daemon lifetime.
- Add fingerprinted prepared-plan LRU.

The systemd unit is provided at `deploy/chronon3d-daemon.service` and already
uses `RuntimeDirectory=chronon3d`.

The unit starts the canonical C++ CLI daemon because that binary owns the
complete `RENDER_JOB` video protocol; the dedicated FlatBuffers binary remains
the lower-level compatibility daemon.

## Verifica

CPU verification on 2026-09-24 with `ccache` and `-j12`:

- `cmake --build build/chronon/linux-fast-dev --target chronon3d_daemon chronon3d_ipc_tests -j12` passes with IPC enabled.
- `chronon3d_ipc_tests --test-case='daemon job lifecycle and health round-trip through simulated FlatBuffers IPC'` passes 1/1 test and 815/815 assertions on the software backend, including the 256-entry terminal-status retention limit and duplicate-prepare failure behavior.
- The simulated wire round-trip covers health, prepare, status, one completed frame, release, post-release status/rejection, daemon draining, and retained failed-prepare status.
- The canonical CLI daemon's plan-backed `RENDER_JOB` IPC accepts an optional
  `job_id`; `GET_JOB_STATUS` exposes `prepared`, `running`, `completed`, and
  `failed` while the job executes and after it returns. A socket integration
  test queries `running` on a second connection, then checks completed and
  rejected-job status using the software renderer. Terminal history is bounded
  to 256 jobs and duplicate active or retained identifiers are refused. This
  extends status visibility to the existing whole-plan execution path without
  claiming that the lower-level FlatBuffers frame API is a video encoder API.

This proves the additive protocol/status path only; it does not satisfy the
plan-backed whole-video factory, worker/encoder, or prepared-job LRU items.

The RenderingGen daemon-vs-CLI integration benchmark passes on the same
software backend (GoldenOverlayJobV2, 240 frames at 1280x720):

- CLI cold: 10012.0 ms; CLI warm: 8976.7 ms.
- Daemon cold: 8816.2 ms; daemon warm: 8800.9 ms.
- Daemon warm is 175.9 ms faster than CLI warm (98.0% of CLI warm time).
- Daemon warm p50/p95/p99 frame time: 19.255 / 34.764 / 57.477 ms.
