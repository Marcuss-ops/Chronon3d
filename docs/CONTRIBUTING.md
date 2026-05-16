# Contributing to Chronon3d

## Core Mantra

> **core only builds, adapters only call, data only flows in.**
>
> **Il Core definisce le leggi. Il Runtime decide il flusso.**
> **I Backend fanno il lavoro concreto. Le Operations chiamano soltanto.**
> **La CLI traduce comandi umani. Tools ed Examples non comandano mai.**

## Guidelines

1. **Keep responsibilities clean**: Don't leak CLI logic into the renderer. Don't leak renderer logic into the scene model.
2. **Deterministic by design**: Any change to the rendering pipeline must be verified for determinism.
3. **Headless first**: The core engine must never depend on windowing or GUI libraries.
4. **Minimal Tests**: Follow the [Testing Philosophy](TESTING.md).

## Build Model

CMake + vcpkg is the primary build path. All dependencies are managed via `vcpkg.json`.

```bash
export VCPKG_ROOT=~/vcpkg
cmake --preset linux-release
cmake --build build/chronon/linux-release -j$(nproc)
```
