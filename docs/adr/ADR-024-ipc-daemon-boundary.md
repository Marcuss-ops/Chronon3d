# ADR-024 — IPC Daemon Boundary (FlatBuffers + Transport Abstraction)

**Status**: ACCEPTED
**Date**: 2026-08-24
**Author**: chronon3d team

## Context

The Chronon3D daemon keeps a warm `RenderEngine` alive across hundreds of
render jobs.  Clients (Go, Rust, Node, Python) need a multi-language
wire protocol.  The existing daemon (in `apps/chronon3d_cli/daemon/`)
uses a custom length-prefixed binary protocol that only Chronon3D
understands.

## Decision

1. **FlatBuffers** as the wire format.  FlatBuffers provides a schema-first,
   multi-language codegen with zero-copy deserialization — ideal for the
   `CreateOnce + per-frame tiny payloads` architecture.

2. **New boundary `src/ipc/`** — transport, codec, and dispatcher live in
   their own module, independently of the CLI, runtime, or render engine.

3. **Transport abstraction** (`IpcTransport` interface) — Unix-domain sockets
   first (Level 1), shared memory / SPSC rings later (Level 2) under the same
   interface.

4. **CompositionSession** (NOT a registry) — per-session compiled composition
   cache using the canonical `CompositionRegistry` for descriptor resolution.
   No new registry is created.

5. **New app `apps/chronon3d_daemon/`** — extracted from the CLI daemon.
   The new daemon has zero CLI dependencies; it links only `chronon3d_ipc` +
   `chronon3d_pipeline`.

## Consequences

- **Positive**: Multi-language clients (Go, Rust, Node) can speak the daemon
  protocol by regenerating from `schema/chronon_ipc.fbs`.
- **Positive**: Per-frame payload is tiny (composition_id + frame_index +
  dynamic params), not full layer lists.
- **Positive**: Transport-agnostic architecture (Unix socket today, SHM
  tomorrow, same interface).
- **Negative**: New dependency (flatbuffers ≈ 2.5 MB static lib).
- **Negative**: IBM (International Business Machines) CLI daemon
  (`chronon_ipc.hpp` custom protocol) becomes legacy — migration path is
  to extract the warm-render logic into `apps/chronon3d_daemon/` and leave
  the CLI daemon as a thin stdin wrapper for backward compatibility.

## Architecture

```
CLIENT (Go / Rust / Node)
        ↓
   FlatBuffers
        ↓
   Unix Socket
        ↓
=====================================
          IPC BOUNDARY (src/ipc/)
=====================================
        ↓
   IpcTransport (interface)
        ↓
   IpcCodec (FlatBuffers ↔ typed)
        ↓
   IpcCommandDispatcher
        ↓
   CompositionSession (session cache)
        ↓
   RenderEngine (warm, persistent)
```

## Files

| File | Purpose |
|------|---------|
| `schema/chronon_ipc.fbs` | FlatBuffers schema for all IPC messages |
| `src/ipc/ipc_transport.hpp` | Transport interface (IpcTransport, IpcClientTransport) |
| `src/ipc/unix_socket_transport.hpp` | Unix-domain socket implementation |
| `src/ipc/ipc_codec.hpp` | Typed request/reply types |
| `src/ipc/ipc_codec.cpp` | FlatBuffers ↔ WireFrame encoding/decoding |
| `src/ipc/composition_session.hpp` | Session-scoped compiled composition cache |
| `src/ipc/ipc_command_dispatcher.hpp` | IpcRequest → Chronon Runtime dispatch |
| `apps/chronon3d_daemon/main.cpp` | Daemon entry point |
| `apps/chronon3d_daemon/CMakeLists.txt` | Daemon build targets |