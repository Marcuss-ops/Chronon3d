# Contributing to Chronon3d

Thanks for your interest! This is a code-first, headless, CPU-only motion graphics engine in C++20.

## Getting Started

### Prerequisites

```bash
# Install build tools
sudo apt-get install -y build-essential cmake ninja-build

# Install ccache for faster rebuilds (optional but recommended)
sudo apt-get install -y ccache
ccache --set-config=max_size=10G
ccache --set-config=compression=true

# ffmpeg for video export
sudo apt-get install -y ffmpeg
```

### Build

1. Fork and clone the repo.
2. Set vcpkg root and fetch the baseline:
   ```bash
   export VCPKG_ROOT=$(pwd)/vcpkg_bootstrap2
   cd vcpkg_bootstrap2 && git fetch --unshallow 2>/dev/null || git fetch origin && cd ..
   ```
   Tip: add `export VCPKG_ROOT=/path/to/Chronon3d/vcpkg_bootstrap2` to your `~/.bashrc`.
3. Configure and build (ccache is auto-detected):
   ```bash
   cmake --preset linux-release
   cmake --build build/chronon/linux-release -j$(nproc)
   ```
4. Run tests: `ctest --preset linux-test`

## Development Workflow

1. **Branch from `main`** — use a descriptive branch name (e.g. `fix/path-registration`, `feat/disk-cache`)
2. **Keep commits focused** — one logical change per commit
3. **Write tests** — new features should include doctest cases; benchmarks go in `tests/bench/`
4. **Run tests before pushing** — `ctest --preset linux-test`
5. **Open a PR** — describe what changed and why

## Code Style

- C++20 with strict compiler flags. The codebase uses exceptions (`std::runtime_error`, `std::invalid_argument`, `std::out_of_range`) for error reporting in registries, asset loading, graph compilation, and other runtime validation paths.
- 4-space indentation, no tabs.
- Follow the naming conventions you see in the codebase:
  - `snake_case` for functions and variables
  - `PascalCase` for types and classes
  - `SCREAMING_SNAKE_CASE` for constants and macros
- Use `//` comments — no `/* */` except for long doc blocks.
- Minimize includes — prefer forward declarations where possible.
- No raw `new`/`delete` — use the framebuffer pool or arena allocators.

## Project Structure

```
include/chronon3d/   — Public headers
src/                 — Implementation files (mirrors include/ layout)
apps/                — CLI tools
tests/               — Doctest-based tests
docs/                — Roadmap and architecture docs
content/             — Composition definitions
```

## Pull Request Checklist

- [ ] Compiles without warnings
- [ ] All existing tests pass
- [ ] New tests added for changed/added code
- [ ] `docs/IMPROVEMENTS.md` updated if relevant
- [ ] CHANGELOG note added (if user-facing change)

## Architecture

Before making changes to core engine files, read:

- [`docs/ARCHITECTURE_EVOLUTION_PLAN.md`](docs/ARCHITECTURE_EVOLUTION_PLAN.md) — the roadmap for modularizing the engine
- [`docs/CORE_OWNERSHIP.md`](docs/CORE_OWNERSHIP.md) — protected files, agent rules, minimum tests, and PR checklist

Key rule: **work in Feature Zone by default** (`content/`, `effects/`, `nodes/`, `assets/`, `video/`, `CLI/`). Touch core files only with explicit justification.

## Questions?

Open an issue or start a discussion. We're friendly.