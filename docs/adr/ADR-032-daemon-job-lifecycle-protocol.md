# ADR-032 — additive daemon job lifecycle protocol

**Status**: Proposed  
**Date**: 2026-09-24  
**Deciders**: Chronon3D team  
**Tags**: IPC, daemon, FlatBuffers, RenderJob

## Context

ADR-024 defines the persistent prepared-composition daemon and frame protocol.
Clients also need explicit prepare, render, release, status, and health messages
with a stable lifecycle state. The canonical CLI daemon already owns whole-video
`RENDER_JOB`; this FlatBuffers protocol must not claim that a frame request is a
whole-video export.

## Decision

Add backward-compatible FlatBuffers union variants for `PrepareJob`,
`RenderJob`, `ReleaseJob`, `GetJobStatus`, and `Health`. In this increment,
`RenderJob` renders one frame of a prepared composition through the existing
`PreparedRenderJob`; its status is retained for `GetJobStatus`. States are
`Unknown`, `Prepared`, `Running`, `Completed`, `Failed`, and `Released`.
`Health` reports daemon readiness independently of render counters.
The frame result is currently consumed in-process and no image/video artifact
is emitted by this API increment.

The existing per-session `CompositionSession` remains the lifecycle owner; no
second composition or job registry is introduced. The commands are additive to
`CreateComposition` and `RenderFrame`. Whole-video plan execution, scheduling,
encoding, and prepared-job eviction remain outside this protocol increment.
The session retains at most 256 released or prepare-failed status tombstones;
prepared jobs remain session-owned until explicitly released.

The canonical CLI daemon's existing plan-backed `RENDER_JOB` path also accepts
an optional transport-level `job_id` and adds `GET_JOB_STATUS` as the next
length-prefixed IPC command. It reports `prepared`, `running`, `completed`, or
`failed` for the full plan job while preserving the existing synchronous
`RENDER_JOB` reply contract. The ID is decoded beside `RenderJobRequest`, not
added to the semantic render request or its fingerprint. A daemon-owned status
history retains at most 256 terminal jobs; active or retained duplicate IDs
are rejected. Requests without an ID remain backward compatible and untracked.

## Consequences

- Existing message discriminants and request/reply variants remain unchanged.
- Job state can be queried after a synchronous frame request and for the most
  recent 256 released or prepare-failed jobs.
- Full plan jobs sent to the canonical CLI daemon can be queried concurrently
  on another socket connection using their caller-provided correlation ID.
- The CLI job ID is transport metadata; it does not change normalized render
  semantics or fingerprint parity.
- `Completed` means the requested frame render returned a framebuffer; it does
  not mean a file was encoded or delivered to the client.
- This protocol does not by itself complete `TICKET-DAEMON-JOB-RUNTIME`.

## Alternatives considered

- Replace the legacy frame protocol: rejected because existing clients depend
  on `CreateComposition` and `RenderFrame`.
- Report whole-video completion from the frame renderer: rejected because it
  would misrepresent the synchronous prepared-frame runtime as an encoder job.

## References

- `docs/adr/ADR-024-ipc-daemon-boundary.md`
- `docs/tickets/TICKET-DAEMON-JOB-RUNTIME.md`
