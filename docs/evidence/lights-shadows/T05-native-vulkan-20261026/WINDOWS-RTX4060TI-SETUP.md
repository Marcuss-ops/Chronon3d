# Windows RTX 4060 Ti build cache

Prepared on 2026-10-26 in `C:\Users\pater\Pyt\Chronon3d-shadow-windows`.
This worktree and its build directory are deliberately persistent; reuse them
for follow-up hardware checks instead of creating a fresh checkout or reinstalling
dependencies.

## Ready-to-use artifacts

- CLI: `build/win-shadow-clang/apps/chronon3d_cli/chronon3d_cli.exe`
- CMake/Ninja cache: `build/win-shadow-clang/`
- Last complete CLI build log: `build/win-shadow-clang-build.log`
- Strict native NVENC attempt: `build/win-shadow-clang/t05-run-native.log`
- Shared preinstalled vcpkg tree:
  `C:/Users/pater/Pyt/Chronon3d-windows-latest/vcpkg_installed/win-release`
- Compiler: `C:/Program Files/LLVM/bin/clang-cl.exe`
- CUDA toolkit: `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6`

## Reuse commands

Run from this worktree in PowerShell. Add the Windows SDK tools, Visual Studio
Build Tools, CUDA `bin`, and shared vcpkg `bin` directories to `PATH` as needed.
Set `CUDA_PATH` to the toolkit above.

```powershell
cmake --build build/win-shadow-clang --target chronon3d_cli --parallel 8
```

The cache has Vulkan, CUDA interop, native FFmpeg/NVENC enabled and
`VCPKG_MANIFEST_INSTALL=OFF`. Preserve that setting and the shared
`VCPKG_INSTALLED_DIR`: enabling manifest installation against that shared tree
caused vcpkg to rebuild cached packages once during setup. Do not run
`vcpkg install` for normal incremental work. The existing compiler launcher uses
ccache, so rebuilding unchanged translation units is also avoided.

To configure a new build, use this cache's settings as a reference and point
`VCPKG_INSTALLED_DIR` and `CMAKE_PREFIX_PATH` at the shared tree above; never
configure with manifest installation enabled against that shared directory.

## Current certification result

The executable initializes the RTX 4060 Ti Vulkan device, CUDA/Vulkan interop,
and native NVENC, and resolves the strict-native execution plan. The one-frame
T05 RenderPlan then fails at `Lighting`: its input has neither a valid native
surface nor CPU pixel backing. NVENC accepts zero frames. This is not a shadow
visual pass and does not close T05. See `t05-run-native.log` in the build tree.
