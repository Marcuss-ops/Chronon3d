# Working Build Host contract

## Purpose

The Working Build Host (WBH) is the canonical machine for Chronon build,
runtime, GPU and release certification. It must be persistent: do not recreate
the build directory, reinstall vcpkg, or discard compiler caches between normal
iterations.

The versioned contract is in [`tools/wbh_toolchain_manifest.env`](../tools/wbh_toolchain_manifest.env).
The non-installing preflight is:

```bash
bash tools/check_wbh_toolchain.sh
```

## Required toolchain

| Component | Contract |
|---|---|
| OS | Linux |
| CMake | >= 3.27 |
| Ninja | >= 1.14; Ninja 1.13 is rejected because of the known rebuild/log issue |
| Compiler | Pinned GCC or Clang version |
| CUDA Toolkit | Pinned version, including `nvcc` |
| NVIDIA driver | Compatible with the pinned CUDA Toolkit |
| Vulkan | Loader + `vulkaninfo` + physical device |
| FFmpeg | Version compatible with the project vcpkg/system contract |
| vcpkg | Commit `cb2981c4e03d421fa03b9bb5044cd1986180e7e4` |
| Hardware | NVIDIA GPU with CUDA, Vulkan, NVDEC and NVENC |
| Caches | Persistent vcpkg binary cache, ccache and build directory |

## Verification sequence

Run on the WBH, from a clean checkout on `main`:

```bash
source tools/wbh_toolchain_manifest.env
bash tools/check_wbh_toolchain.sh
```

The script verifies CMake, Ninja, `nvcc`, Vulkan, NVIDIA GPU and compiles/runs
`tools/cuda_smoke.cu`. If the NVENC probe executable exists, it also requires:

```text
CUDA_VULKAN_NVENC_PASS
```

The probe is built by the Vulkan-enabled CMake configuration. It verifies the
Vulkan external-memory → CUDA frame → `h264_nvenc` handoff without a host pixel
buffer.

## Clean certification build

Use a persistent, named build directory for the canonical host. The first
certification must be clean:

```bash
cmake -S . -B .tmp/wbh-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/Chronon3DVcpkgToolchain.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DCHRONON3D_BUILD_TESTS=ON \
  -DCHRONON3D_BUILD_CLI=ON \
  -DCHRONON3D_ENABLE_VULKAN=ON \
  -DCHRONON3D_ENABLE_CUDA_INTEROP=ON
cmake --build .tmp/wbh-release -j
ctest --test-dir .tmp/wbh-release --output-on-failure
```

Then verify no-op and incremental behavior:

```bash
cmake --build .tmp/wbh-release -j
# touch one source file in a controlled test checkout, then:
cmake --build .tmp/wbh-release -j -- -d explain
```

The incremental result must rebuild only the affected dependency closure, not
all Vulkan objects. Record configure/build/test durations and peak memory in
the WBH certification artifact.

## Current host observation (2026-08-28)

The current environment is **not certification-capable**:

- CMake `3.31.6` — satisfies the minimum.
- Ninja `1.13.0.git.kitware.jobserver-pipe-1` — rejected; upgrade to the pinned
  approved `>=1.14` release.
- NVIDIA RTX A4000, driver `595.84`, UUID
  `GPU-66101dcc-7b1c-e277-889c-c62462e0ac8e` — detected.
- Vulkan loader and `vulkaninfo` — available; one ICD warning is emitted and
  must be reviewed on the canonical host.
- `nvcc` — missing; install the complete pinned CUDA Toolkit, not only the
  driver/runtime libraries.
- Existing CUDA/Vulkan/NVENC probe evidence is documented in
  `docs/CURRENT_STATUS.md`, but it does not replace the missing `nvcc` smoke
  compilation and same-SHA release certification.

The preflight intentionally stops at the first missing requirement and does
not install packages or modify the host. Installation must be performed by the
WBH owner using the approved OS/toolchain provisioning process.

## Main push synchronization contract

All work lands directly on `main`; branches and force-pushes are not part of the
workflow. `tools/wrap_push.sh` is fail-closed:

```bash
CHRONON3D_TESTED_SHA=<sha-tested> tools/wrap_push.sh origin main
```

The wrapper rejects force-push flags on `main`, rejects a stale tested SHA, and
after the push verifies:

```text
CHRONON3D_TESTED_SHA == HEAD == origin/main == @{u}
```

If `HEAD` is ahead of `origin/main` with a linear ancestor relation, the
remaining operation is a normal fast-forward push after the worktree is clean.
If both sides diverge, resolve explicitly before retrying; never use
`--force` blindly.

## Definition of done

```text
pinned toolchain recorded
Ninja >= 1.14
nvcc --version matches pinned CUDA
cuda_smoke.cu compiles and runs
vulkaninfo identifies the expected GPU
CUDA/Vulkan UUID match recorded
CUDA_VULKAN_NVENC_PASS
clean build PASS
CTest PASS
incremental no-op PASS
controlled incremental rebuild measured
persistent caches verified
```
