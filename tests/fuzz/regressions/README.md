# Fuzz regression corpus

Every crash discovered by a libFuzzer target becomes a **permanent regression
test**.  The workflow is:

```
fuzzer discovers a crash input
        ↓
copy the crashing input here
        ↓
CMake auto-registers a CTest test
        ↓
CI runs it on every build — the bug can never silently regress
```

## Directory layout

```
regressions/
├── ipc_codec/               → replay with tests/fuzz/ipc_codec_fuzz
│   └── heap_overflow_decode_reply.bin
├── maybe_expression/        → replay with tests/fuzz/maybe_expression_fuzz
└── composition_descriptor/  → replay with tests/fuzz/composition_descriptor_fuzz
```

One subdirectory per fuzz target.  The subdirectory name maps 1:1 to the fuzz
target binary (see `tests/fuzz/CMakeLists.txt`, `chronon3d_add_fuzz_regressions`).

## Adding a regression

1. Run a fuzz target until it finds a crash (libFuzzer writes
   `crash-<sha>` / `artifact-<sha>` into the working directory, or the
   `-artifact_prefix=` directory):

   ```bash
   ./ipc_codec_fuzz tests/fuzz/corpus/ipc/ -max_total_time=300
   ```

2. Rename the crash input to a descriptive name and place it under the matching
   target subdirectory:

   ```bash
   cp crash-0123456789abcdef tests/fuzz/regressions/ipc_codec/heap_overflow_decode_reply.bin
   ```

3. Re-run CMake configure (the `file(GLOB ... CONFIGURE_DEPENDS)` picks up the
   new file automatically) and verify the test is registered and green:

   ```bash
   ctest -R fuzz_regression_ --output-on-failure
   ```

## Semantics

Each input is replayed **once** (`-runs=1`) against its target under
ASan+UBSan.  A clean exit (`0`) means the crash is still fixed; a sanitizer
abort (non-zero) means the bug has regressed and the test FAILS.

> The test asserts the input **no longer crashes**.  A regression means the
> underlying fix was reverted — the test turns red, exactly as intended.

## CI

The `fuzz-regressions` job in `.github/workflows/ci.yml` configures with
Clang + `CHRONON3D_BUILD_FUZZERS=ON`, builds all three fuzz targets, and runs
`ctest -L fuzz-regression`.  No separate corpus sync is needed: the inputs are
committed to the repo.
