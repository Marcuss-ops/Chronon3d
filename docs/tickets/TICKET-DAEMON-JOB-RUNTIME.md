# TICKET-DAEMON-JOB-RUNTIME — persistent C++ daemon authority

## Stato

PARTIAL — persistent prepared-frame runtime landed; the canonical CLI daemon
already provides the job-oriented video IPC path, while the dedicated
FlatBuffers daemon still exposes the compatibility frame protocol.

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

## Prossimo blocco

- Extend FlatBuffers with `PrepareJob`, `RenderJob`, `ReleaseJob`,
  `GetJobStatus`, and `Health`.
- Replace the placeholder composition descriptor with a real plan-backed
  composition factory.
- Add bounded scheduler/worker ownership and an encoder adapter for whole-video
  rendering, preserving Vulkan/CUDA/NVENC resources for daemon lifetime.
- Add fingerprinted prepared-job LRU and systemd unit with `RuntimeDirectory`.

The systemd unit is provided at `deploy/chronon3d-daemon.service`.

The unit starts the canonical C++ CLI daemon because that binary owns the
complete `RENDER_JOB` video protocol; the dedicated FlatBuffers binary remains
the lower-level compatibility daemon.

## Verifica

`cmake --build build/chronon/linux-video-release --target chronon3d_daemon -j4`
passes locally after the PreparedRenderJob migration. The IPC test target is
not enabled in this build configuration (`No tests were found`).

The RenderingGen daemon-vs-CLI integration benchmark passes on the same
software backend (GoldenOverlayJobV2, 240 frames at 1280x720):

- CLI cold: 10012.0 ms; CLI warm: 8976.7 ms.
- Daemon cold: 8816.2 ms; daemon warm: 8800.9 ms.
- Daemon warm is 175.9 ms faster than CLI warm (98.0% of CLI warm time).
- Daemon warm p50/p95/p99 frame time: 19.255 / 34.764 / 57.477 ms.
