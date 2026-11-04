# Chronon 2.0 repository and toolchain snapshot

Observed 2026-11-03 UTC for P0-01. This records the working environment; it is
not a frozen or reproducible release baseline. The three working trees were
not clean, and no clean-checkout build was run as part of this snapshot.

## Source revisions

| Repository | HEAD | Working-tree state |
|---|---|---|
| C3D (`Chronon3d`) | `b1b21cbed1d8927de8929a33924c1f4d097955c4` | Dirty; 35 status entries at the 2026-11-04 refresh, including renderer changes and evidence |
| CM3D (`ChrononMotion3d`) | `b7137ee0094e7f750a287305ac30b0273c583b22` | Dirty; 5 status entries, including producer/test changes, evidence and pre-existing generated `__pycache__/` |
| ChrononTemplate | `80959bbcdcd4df8ea5c3705a04e9791ae18a151f` | Dirty; 5 untracked entries for backgrounds, Liberation Mono font/license and two important-phrases tools |

The exact `git status --short` snapshots for all three worktrees are preserved
in [`workspace-status-20261104.txt`](workspace-status-20261104.txt). The SHAs
above are still local worktree bases, not release commits.

The proposal's C3D short SHA `7fb9fd0a` does not match the observed checkout.
The observed full SHA is authoritative for this local snapshot only.

## Dependency and build inputs

| Repository | Inputs found | SHA-256 / pin |
|---|---|---|
| C3D | `vcpkg.json` | `96c8170a3fc5b53b2b7117574e5df9a4496decd121f00eb67465e89f870cb769`; builtin baseline `cb2981c4e03d421fa03b9bb5044cd1986180e7e4` |
| C3D | `CMakePresets.json` | `af50459075c189f8b0a7140967c83c569817cda044079f4ccfaba4a8eb1cae04` |
| C3D | `sdk_consumers/04_rust/Cargo.lock` | `92e99773c556d28986381cb8bf270b367e73965f6177e8885420762b872f1e27` |
| CM3D | `CMakePresets.json` | `f5bff0e01a3542e652ae4901a34060b8fcccc7043d0b6791629ce7923b28ec8f` |
| ChrononTemplate | `CMakePresets.json` | `53a04b4e342b1173bc5325053e6351c4edccc2f760e8acbb54cbf0d13f3526c5` |

No additional package lockfile or vcpkg manifest was found at repository depth
3 for CM3D or ChrononTemplate. This is an inventory, not proof that every
transitive build input is pinned.

## Toolchain and GPU observation

- Development host: Ubuntu 26.04.1 LTS x86_64.
- CMake 4.2.3; Ninja 1.13.2; GCC 15.2.0.
- Remote host alias `gpu-node`: NVIDIA RTX A4000, 16,376 MiB, driver 595.99.02.
- `vulkaninfo --summary` on that host lists both NVIDIA and llvmpipe; the
  NVIDIA ICD was explicitly selected. At observation time, 5,570 MiB VRAM was
  in use. No Chronon render was run.
- Remote host OS recorded by the existing workflow on 2026-11-02:
  Ubuntu 22.04.5 LTS x86_64.

## P0-01 disposition

**PARTIAL.** Scope and source/toolchain snapshot are recorded. P0-01 cannot be
marked closed while the working trees are dirty and a clean, reproducible
build has not been demonstrated. Do not use this document as a release tag
manifest or as evidence of same-SHA CI.

## Local C3D CLI build observation — 2026-11-04

The configured fast-dev CLI present in the worktree has unstripped SHA-256
`8a5d41159588c06e2bd6d7480b2916240a1d4acd6458e3f44e420fb65f3c7a20` and
modification time 2026-11-04 00:53 UTC. Its stripped GPU-node copy has SHA-256
`f3f4010eb13e19e6d5c9bbf440fe7dabb6cbf07d7f090fd3e643cfd18a31486c`. The
renderer source changes in this worktree predate that executable; later
changes for the P0-09 stress case are test-only. This local configuration has
`CHRONON3D_ENABLE_CUDA_INTEROP=OFF`, so it cannot probe the native NVENC route.

A fresh CLI rebuild was attempted from `/tmp/chronon3d-linux-video-test` with
Vulkan enabled and CUDA interop disabled. At `-j12`, the linker/archiver was
terminated with exit 137 while creating the large debug static library; retry
at `-j2` was also terminated with exit 137 during archive creation. No clean
CLI rebuild is claimed from those attempts.

The binary can run Vulkan readback fixtures on the A4000, but strict GPU-native
video is not supported by this build configuration. P0-02 remains open for a
clean-checkout build and same-SHA CI; P0-01 remains **PARTIAL** until there is
a frozen clean source revision and reproducible build manifest.

## GPU-host release CLI build — 2026-11-04

To use the host's CUDA SDK without modifying its other Chronon worktree, the
current C3D source was mirrored to
`/home/pierone/chronon2-p010-c6/chronon3d-src` (59,282,796 bytes; docs, tests,
build outputs and vendored dependency trees excluded). C3D base HEAD was
`b1b21cbed1d8927de8929a33924c1f4d097955c4`; the binary-relevant tracked
worktree diff hash (`git diff --binary HEAD -- include src apps cmake
CMakeLists.txt CMakePresets.json vcpkg.json`) was
`02d5da0c988c1e6e0bdb5406ae57dbafd9b173c5d748e04465a608691fa55318`.

CMake 3.31.6, GCC 11.4.0 and Ninja configured the `linux-video-release`
preset with `CHRONON3D_ENABLE_CUDA_INTEROP=ON`, CUDA/NVRTC from `/usr/include`
and the host driver libraries. The existing installed release vcpkg tree was
reused with `VCPKG_MANIFEST_MODE=OFF`; this avoids a package install and is
not a clean or independently reproducible dependency build. `ninja -j12
chronon3d_cli` completed **683/683 steps, exit 0**. The resulting CLI identifies
as `Chronon3d 0.1.0 (chronon.render-plan.v2)` and has SHA-256
`f22f9b5e1d3cc02bf4deea5c7c89ca9f3aff6a2a85ca5e7c639b7b61ed679a94`.

This closes a local CUDA-enabled CLI build sub-gate, not P0-02: the copied tree
is dirty, tests and SDK were not built, manifest mode was disabled, and no
same-SHA CI ran. The full optical raw run and strict-native NVENC acceptance
are tracked separately under P0-10/P0-13.
