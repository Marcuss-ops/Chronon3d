# 01_cpp_minimal

Minimal C++ SDK consumer. Links only `Chronon3D::SDK` (via
`find_package(Chronon3D)`) and renders the canonical RenderPlan JSON through
the thin facade `compile_plan_json` + `render_compiled`.

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$SDK_PREFIX"
cmake --build build
./build/cpp_minimal plan.json
# CPP_SDK_CONSUMER_PASS 64x64
```

It never includes an umbrella header or links an internal target — the single
canonical include is `<chronon3d/sdk/render_engine.hpp>`.
