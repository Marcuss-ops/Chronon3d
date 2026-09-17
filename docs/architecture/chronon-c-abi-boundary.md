# Chronon cross-repository C ABI boundary

## Authority

`include/chronon/abi.h` is the only canonical public contract between the
Chronon renderer and Chronon3D. It contains opaque handles, fixed-width
integers, POD records, callbacks, and status values. It deliberately contains
no C++ classes, STL types, renderer/text/image/video subsystems, scene graph,
cache, font, or camera declarations.

`include/chronon3d/c_api/chronon3d.h` is retained only as a compatibility
forwarding include. It must not acquire declarations of its own.

## Ownership and process boundary

- `chronon_engine*` and `chronon_plan*` are opaque and owned by the creating
  API. Destroy them with the matching destroy function.
- `chronon_render_frame_into` writes to caller-owned storage; it retains no
  pointer to that storage. The size-query call reports the required POD frame
  metadata.
- Engine calls are serialized per handle; concurrent calls on one engine fail
  with `CHRONON_ERROR_BUSY`. Separate engines may run concurrently.
- JSON is a same-process API input. If Chronon later runs as a service, the
  serialized RenderPlan becomes the transport contract; the C ABI is not used
  as an RPC protocol.
- ABI major changes require changing `chronon_abi_version()`, the shared
  library SOVERSION, and the symbol baseline together. Additive symbols are
  allowed within ABI major 2.

## Repository boundary

Chronon owns text, image, video, asset resolution, and rendering semantics.
Chronon3D owns scene/layer authoring, animation, and camera authoring. A
Chronon3D integration may include only `<chronon/abi.h>` for the cross-repo
contract; implementation sources may include their private adapter headers,
but those headers are not installed or exposed to SDK consumers.

Same process uses the C ABI. A future different-process deployment must use a
versioned serialized plan/IPC contract while preserving the same semantic
ownership split.

## Verification

```bash
bash tools/sdk/check_cabi_header.sh
```

The installed-package C ABI gates additionally verify the exported symbol
baseline, SONAME, self-contained runtime dependencies, and the C/C++/Go/Rust/
Python consumer projects.
