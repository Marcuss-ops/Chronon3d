# Windows RTX 4060 Ti CUDA smoke — 2026-10-11

## Host and toolchain

- Host: `Youtube_King`, reached from Linux over SSH on the existing Tailscale network.
- GPU: NVIDIA GeForce RTX 4060 Ti, 8188 MiB reported by `nvidia-smi`.
- NVIDIA driver: 610.88.
- CUDA Toolkit: 12.6 (`nvcc`).
- Host compiler: Visual Studio 2026 Community 18.4.2.

## Compute probe

Compiled a temporary CUDA program on the Windows host. It allocated three
1,048,576-element float buffers, copied two initialized vectors to the device,
launched a CUDA addition kernel, synchronized, copied the output back, and
checked every element against `3.75`.

`nvcc` initially rejected the installed Visual Studio 2026 compiler because
CUDA 12.6 only declares support through Visual Studio 2022. The smoke probe was
then compiled with NVIDIA's explicit `-allow-unsupported-compiler` override.
This override is recorded as a compatibility limitation and is not a
production-build certification.

Observed output:

```text
CUDA_KERNEL_PASS device=NVIDIA GeForce RTX 4060 Ti count=1048576 sample=3.75
```

Exit status: 0. The test executable ran on the Windows host, not on the Linux
Chronon3d checkout. Temporary source, batch file, and executable were removed
from the Windows Temp directory after the run.

## Scope

This verifies that the Windows driver and CUDA runtime can execute a real
kernel on the RTX 4060 Ti. It does not certify Chronon3d's Vulkan renderer,
light/shadow/particle parity, or a production Chronon3d Windows build. The
Windows host has only the isolated source archive used for the build attempt
below; the GPU remains a separately reachable compute host, not a Linux-local
GPU.

Chronon3d source revision observed during the probe: `e8e06f5b315f4ffe39e6819e926dcfc1156b8408`.

## Chronon3d Vulkan build attempt

The isolated source archive was configured on Windows with
`CHRONON3D_ENABLE_VULKAN=ON`. vcpkg dependencies installed, Vulkan 1.4.341 was
detected, and all 46 compute shaders were compiled and validated to SPIR-V.
The baseline Vulkan manifest did not request the `glslang[tools]` feature even
though `ChrononShaders.cmake` requires `glslangValidator`; this was added only
to the isolated copy to continue the build check. Enabling the test tree also
hit an MSVC-incompatible `$<LINK_GROUP:RESCAN>` link feature, so this check
configured the CLI without tests.

The first CLI build stopped because MSVC could not find `sys/mman.h`. A limited
Windows portability pass was then made in the Linux checkout and copied to the
isolated Windows tree: `VirtualAlloc`/`VirtualFree`, Windows time conversion,
restrict spelling, and a Windows self-pipe path. Those changes passed their
focused memory smoke and the affected core sources compiled. Continuing the
application build then exposed the wider MSVC mismatch: `__int128` is used in
seven production files, and MSVC does not implement that extension. The source
also contains `constexpr` functions calling `std::abs`, which this toolchain
rejects. The Windows CLI did not link. Further Windows porting was stopped by
the user; no Chronon3d Windows binary or renderer run is claimed.

The Windows Vulkan loader can enumerate the NVIDIA GeForce RTX 4060 Ti, but
Chronon3d did not reach runtime. The CUDA kernel smoke proves the GPU is usable;
Chronon3d Vulkan execution on Windows remains unverified.

## WSL follow-up

Ubuntu 24.04 and 26.04 were installed in WSL 2. In WSL, `nvidia-smi` sees the
same RTX 4060 Ti and `/dev/dxg` plus `/usr/lib/wsl/lib/libcuda.so.1` are
available. Vulkan loader diagnostics in Ubuntu 24.04 enumerate only
`llvmpipe (LLVM 20.1.2, 256 bits)`; the installed ICDs do not expose the NVIDIA
GPU to Vulkan. WSL therefore provides CUDA access, but does not currently
provide the Vulkan device needed to certify Chronon3d's Vulkan renderer.

Follow-up inspection found WSL 3.0.1, WSLg 1.0.79, Ubuntu 24.04.5, and Mesa
`mesa-vulkan-drivers` 25.2.8. `/dev/dxg` is present, while the distro has no
`libvulkan_dzn.so` or DZN ICD. The installed Vulkan ICD list contains `lvp`
and drivers for other Linux GPU stacks. This explains why Vulkan selects
llvmpipe even though `nvidia-smi` and CUDA work in WSL. Mesa Dozen (DZN) is a
Vulkan-over-D3D12 route, but is experimental; [a WSLg report](https://github.com/microsoft/wslg/issues/1492)
describes a `vkCreateDevice` crash with NVIDIA driver 610.88 and WSLg 1.0.77.
We did not install that driver path.

## Windows allocator follow-up

The Linux checkout now has a `_WIN32` allocator path using `VirtualAlloc` and
`VirtualFree`; the POSIX mmap path remains under the non-Windows branch. A
temporary MSVC smoke program allocated, wrote, and freed a 1 MiB block, then
checked 64-byte alignment and allocate/deallocate through `HugePageAllocator`.
It printed `MEMORY_WINDOWS_SMOKE_PASS` and exited 0.

Rebuilding the Chronon3d CLI with that header advanced past the original
`sys/mman.h` failure, then stopped on other Windows portability errors,
including `gmtime_r` and `__restrict__`. Those were corrected in the checkout;
the isolated Windows build now compiles the affected core sources and all 46
shader-generation steps. After initializing the VS 2026 x64 environment and
removing an archive-only root file named `version` that shadowed the standard
C++ `<version>` header, the build reached the media targets and stopped on
MSVC's lack of `__int128` support in `include/chronon3d/core/types/time.hpp`.
`include/chronon3d/math/expression_types.hpp` also has `constexpr` functions
calling `std::abs`, which MSVC rejects. The Windows CLI has not linked or run;
Chronon3d Vulkan execution on the RTX remains unverified.
