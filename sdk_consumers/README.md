# sdk_consumers/

Permanent, multi-language **SDK consumer mini-projects**. Each one pretends to
know nothing about the Chronon3D internals: it consumes only the **installed
package** (headers + `libchronon3d_c.so` / `Chronon3D::SDK`), never the
source tree.

Every project renders a tiny, self-contained RenderPlan (a single `color`
layer, no fonts/images/audio) and asserts a non-empty frame, so they run with
zero external assets.

| Dir            | Language | Integration surface                          | Entry point            |
|----------------|----------|-----------------------------------------------|------------------------|
| `01_cpp_minimal` | C++      | `find_package(Chronon3D)` → `Chronon3D::SDK` + RenderPlan facade | CMake                |
| `02_c_minimal`   | C        | C ABI (`chronon3d.h`) via pkg-config / Makefile | `make`                |
| `03_go`          | Go       | C ABI via `cgo` + `#cgo pkg-config: chronon3d` | `./run.sh`            |
| `04_rust`        | Rust     | C ABI via hand-written FFI + `build.rs`        | `./run.sh`            |
| `05_python`      | Python   | C ABI via `ctypes` (no third-party packages)   | `./run.sh`            |

## Contract

1. **Only the installed package.** None of these projects reference
   `../../include`, `../../src`, in-tree CMake targets, or any
   `chronon3d_*_impl` / `chronon3d_graph` / `chronon3d_pipeline` target.
2. **One canonical plan protocol.** They all drive the same
   `chronon.render-plan` v1 JSON (`plan.json`) through the C ABI
   (`chronon_plan_compile_json_n`) or the C++ facade
   (`sdk::RenderEngine::compile_plan_json`).
3. **Self-contained C ABI.** They link only `libchronon3d_c.so` (all
   third-party deps are statically linked inside it), so Go/Rust/Python need
   no `glm`/`harfbuzz`/`freetype`/`blend2d`/`xxhash` dev packages.
4. **Single success marker.** Each prints exactly one `*_CONSUMER_PASS`
   marker on success and exits 0; any failure exits non-zero.

## Building against an installed prefix

Point each project at an installed SDK prefix via `SDK_PREFIX`
(`CHRONON3D_PREFIX` is also accepted):

```bash
export SDK_PREFIX=/opt/chronon   # or wherever you ran cmake --install --prefix
```

- **C++**: `cmake -S 01_cpp_minimal -B build -DCMAKE_PREFIX_PATH="$SDK_PREFIX" && cmake --build build`
- **C**:   `make -C 02_c_minimal`
- **Go**:  `03_go/run.sh`
- **Rust**: `04_rust/run.sh`
- **Python**: `05_python/run.sh`

These are the same projects the release gate
`tools/verify_sdk_product.sh` runs against the installed package.
