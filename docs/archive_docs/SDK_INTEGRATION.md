# Chronon3D integration surfaces

Chronon exposes one renderer through three supported boundaries:

| Consumer | Boundary |
|---|---|
| C++ application | `Chronon3D::SDK` from the installed CMake package |
| Go/Rust/Python/Node | `libchronon3d_c.so` and `chronon3d/c_api/chronon3d.h` |
| Worker process | `chronon3d_cli render-plan --input plan.json` |

All three boundaries resolve the same `Composition` and render through the
canonical engine. The JSON contract is installed at
`share/chronon3d/schemas/chronon.render-plan.v1.schema.json`.

## Worker invocation

```bash
chronon3d_cli render-plan \
  --input /work/render-plan.json \
  --assets-root /work/assets \
  --output /work/output/final.mp4
```

The command exits non-zero on parse, asset, codec, cancellation or output
errors. Video output is first written to a temporary sibling and is published
atomically only after the encoder closes successfully.

## C ABI ownership

`chronon_engine`, `chronon_plan` and frame buffers are opaque. Chronon owns
their allocations; release plans with `chronon_plan_destroy` and buffers with
`chronon_buffer_free`. Strings are UTF-8 and callbacks use C function pointers.
The ABI version is independent from the product version and is returned by
`chronon_abi_version()`.
