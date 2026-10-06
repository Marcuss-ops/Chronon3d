# WBH certification protocol

## Purpose and authority

The Working-Build-Host (WBH) profile qualifies the machine, then runs the
repository's full developer, CI, and WBH-only gates against one source revision.
`tools/wbh_toolchain_manifest.env` is the machine contract. The gate membership
authority remains `tools/gates/manifest.sh`; do not maintain a second gate list.

This protocol does not claim NVIDIA certification from Mesa/llvmpipe, nor does a
passing machine check replace the GitHub Actions same-SHA run. Those results
must be recorded separately.

## Machine qualification

From the repository root, configure the `linux-video-test` preset and verify the
active tools and CMake options:

```bash
cmake --preset linux-video-test
python3 tools/check_wbh_toolchain.py \
  --build-dir build/chronon/linux-video-test --require-clean
```

The checker validates OS and architecture, minimum CMake/Ninja/GNU C++/Python
versions, `glslangValidator`, build type, generator, and required text/video/
Vulkan/verification options. It prints the exact HEAD SHA and a digest of the
worktree status. `--require-clean` is mandatory for certification: a dirty tree
can qualify its toolchain, but cannot certify a commit.

## Gate sequence

1. Record the output of the machine qualification command above.
2. Run the full developer chain (including slow/build gates):

   ```bash
   bash tools/run_developer_gates.sh --full origin main
   ```

3. Run CI build and unit-test phases using the exact workflow commands in
   `.github/workflows/ci.yml` for the target profile. For Linux video, use
   `.github/workflows/native-video.yml` and preserve the exact preset, target,
   test filter, and logs.
4. Run every member of `WBH_ONLY_GATES` in
   `tools/gates/manifest.sh` from the repository root:

   ```bash
   source tools/gates/manifest.sh
   for gate in "${WBH_ONLY_GATES[@]}"; do bash "tools/${gate}"; done
   ```

5. Re-run machine qualification with `--require-clean` and confirm its SHA is
   unchanged. Record date, host ID, OS/architecture, manifest values, SHA,
   command lines, exit status, and log paths in a dated evidence file under
   `docs/evidence/`. Never describe a partial gate set as a full WBH pass.

Do not use `tools/wrap_push.sh` as a certification command: after gates it pushes
to the configured remote. This protocol runs validation without publishing.

## Current machine qualification

On 2026-10-05, the checked-in manifest passed 13 checks on Linux x86_64 with
CMake 4.2.3, Ninja 1.13.2, GNU C++ 15.2.0, Python 3.14.4, and glslangValidator
11:16.2.0. The configured Debug `linux-video-test` build has text, video,
Vulkan, and verification enabled. This was a toolchain qualification only: the
shared worktree was dirty, so exact-SHA certification was refused/not claimed.
The repository still needs a clean-tree run of the full sequence and matching
remote CI evidence.
