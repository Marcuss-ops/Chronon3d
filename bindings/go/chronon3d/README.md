# Chronon3D Go bindings

cgo bindings over the Chronon3D C ABI (`<chronon/abi.h>`, ABI major 2).

## Linking

- **Installed SDK (default):** `#cgo pkg-config: chronon3d` — make sure
  `PKG_CONFIG_PATH` contains `<prefix>/lib/pkgconfig`.
- **In-tree build:** build with `-tags chronon3d_manual` and provide flags:

```bash
export CHRONON3D_LIB=$PWD/build/chronon/<preset>/libchronon3d_c.so
LIBDIR=$(dirname "$CHRONON3D_LIB")
CGO_CFLAGS="-I$PWD/include" \
CGO_LDFLAGS="-L$LIBDIR -lchronon3d_c -Wl,-rpath,$LIBDIR" \
go test -tags chronon3d_manual ./bindings/go/chronon3d/...
# or simply: bindings/run_binding_tests.sh go
```

## Usage

```go
eng, err := chronon3d.NewEngine(&chronon3d.Options{AssetsRoot: "assets"})
if err != nil { return err }
defer eng.Close()

plan, err := eng.CompilePlan(planJSON) // or CompilePlanFile(path)
if errors.Is(err, chronon3d.ErrInvalidPlan) { ... }
defer plan.Close()

frame, err := eng.RenderFrame(plan, 0) // frame.Pix is Go-owned

err = eng.RenderSequence(ctx, plan, 0, 29, func(f *chronon3d.Frame) error { ... })

// ctx cancellation is forwarded to the ABI is_cancelled callback.
err = eng.RenderFile(ctx, plan, "out.mp4", &chronon3d.RenderFileOptions{
    Progress: func(cur, total uint64) { ... },
})
if errors.Is(err, context.Canceled) { ... } // also matches chronon3d.ErrCancelled
```

Errors are `*chronon3d.Error` (Status, Message, Code, Component, NodeID, Asset);
`errors.Is(err, chronon3d.ErrXxx)` matches by status. `Engine` is safe for
concurrent use (calls are serialized).

## ABI limitations

- Only RenderPlan JSON input; assets root only at creation (`SetAssetsRoot`
  recreates the engine and invalidates plans).
- Cancellation is cooperative: polled by the engine only inside `RenderFile`;
  `RenderFrame` is not interruptible, `RenderSequence` checks ctx between frames.
