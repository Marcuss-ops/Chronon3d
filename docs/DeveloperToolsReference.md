# Chronon3D Developer Tools Reference

This document maps all the build tools, compilers, and caching utilities available on this system, and explains how to optimize the build process to achieve up to **10x faster compilation times**.

---

## 🛠️ Toolchain Map

Here are the precise paths of the tools discovered on this system:

| Tool | Path | Purpose |
| :--- | :--- | :--- |
| **CMake** | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` | Build configuration system (VS 2022 native) |
| **Ninja** | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe` | High-speed build system (VS 2022 native) |
| **C++ Compiler (MSVC)** | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx86\x86\cl.exe` | C++ Compiler |
| **MSBuild** | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe` | Microsoft Build Engine |
| **vcpkg** | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg\vcpkg.exe` | C++ package manager |
| **sccache** | `C:\Users\pater\AppData\Local\Microsoft\WinGet\Packages\Mozilla.sccache_Microsoft.Winget.Source_8wekyb3d8bbwe\sccache-v0.15.0-x86_64-pc-windows-msvc\sccache.exe` | Shared Compilation Cache (from Mozilla) |

> [!WARNING]
> There are multiple versions of CMake and Ninja installed (including a slower Android SDK version at `C:\Users\pater\AppData\Local\Android\Sdk\cmake\4.1.2\bin`). Always prioritize the **Visual Studio 2022** native versions for better performance and MSVC compatibility.

---

## ⚡ Why do compiles take so long by default?

By default, CMake and MSBuild build projects and files **sequentially (one by one)** without utilizing the full potential of your high-performance **AMD Ryzen 16-Core Processor**.

---

## 🚀 How to Compile 10x Faster

### 1. Enable Parallel Compilation (CPU Cores)
Always pass the parallel execution flags to utilizing all 16 cores (32 threads):
* **With CMake build command:**
  ```powershell
  cmake --build build_vs --config Release --parallel 16
  ```
* **Directly with MSBuild:**
  ```powershell
  cmake --build build_vs --config Release -- /m
  ```

### 2. Enable `sccache` (Compiler Caching)
`sccache` acts as a compiler cache. When you compile, it stores the results of compiled objects. If you rebuild or edit unrelated files, the cached compilation is retrieved in **milliseconds** rather than recompiling!

To configure CMake to use `sccache`, run:
```powershell
cmake -B build_vs -S . -DCMAKE_CXX_COMPILER_LAUNCHER="C:\Users\pater\AppData\Local\Microsoft\WinGet\Packages\Mozilla.sccache_Microsoft.Winget.Source_8wekyb3d8bbwe\sccache-v0.15.0-x86_64-pc-windows-msvc\sccache.exe"
```

---

## 🏎️ Fast Build Script: `build_fast.ps1`

We have created an automated fast-build script `build_fast.ps1` in `tools/`. This script automatically configures CMake to use `sccache` and builds the project in parallel using all CPU cores.

Run it directly from PowerShell:
```powershell
.\tools\build_fast.ps1
```
