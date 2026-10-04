# Chronon3D Python bindings

Pure-Python (`ctypes`) bindings over the Chronon3D C ABI (`<chronon/abi.h>`, ABI major 2).
No compiled extension: you only need `libchronon3d_c.so` (built with `-DCHRONON3D_BUILD_C_API=ON`).

## Library discovery

1. `CHRONON3D_LIB` — path to the library file (or the directory containing it)
2. `pkg-config --variable=libdir chronon3d` (installed SDK, `chronon3d.pc`)
3. the system loader search (`ctypes.util.find_library("chronon3d_c")`)

## Usage

```python
from chronon3d import RenderEngine, InvalidPlanError, CancelledError

plan = {...}  # RenderPlan v2 dict, JSON str/bytes, or path to a .json file

with RenderEngine(assets_root="assets") as eng:
    frame = eng.render_still(plan, frame=0)          # Frame(width, height, stride, pixel_format, data)
    with eng.compile(plan) as p:                     # compile once, render many
        for f in eng.render_sequence(p, range(10)):
            ...
        eng.render_file(p, "out.mp4", start=0, end=89,
                        progress=lambda cur, total: print(cur, total),
                        cancelled=stop_event)        # threading.Event or callable
```

Errors raise subclasses of `ChrononError` (one per `chronon_status`, e.g.
`InvalidPlanError`, `AssetNotFoundError`, `CancelledError`) carrying `status`,
`message`, `code`, `component`, `node_id`, `asset` from `chronon_error_info`.

`RenderEngine` is thread-safe (calls are serialized with a lock, since the C
engine returns `BUSY` on concurrent use).

## Notes / ABI limitations

- Only RenderPlan JSON is accepted by the C ABI (no composition-registry entry point).
- `set_assets_root()` recreates the native engine (the ABI only takes it at creation) and closes existing plans.
- `render_file` output format is decided by the engine from the extension; video requires a build with video enabled.

## Tests

```bash
pip install pytest
CHRONON3D_LIB=build/chronon/<preset>/libchronon3d_c.so python -m pytest bindings/python
# or: bindings/run_binding_tests.sh python
```
