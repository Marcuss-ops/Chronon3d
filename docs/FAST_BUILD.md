# Fast Build — Sub‑30s Incremental Workflow

> Goal: a typical **incremental rebuild** of `chronon3d_dev_fast` (CLI + fast
> tests) lands **well under 30 s**, often around **13–17 s** on Linux with
> ccache warmed up and the incremental build directory already configured. Cold first‑build still takes
> several minutes — that is normal and expected.

This page covers the **operator workflow** that backs those numbers. The
implementation lives in two places:

| Component | Where | What it does |
|---|---|---|
| Wrapper script | `./build-fast.sh` | resolves the build dir, bootstraps ccache, invokes Ninja |
| CMake presets | `CMakePresets.json::linux-fast-dev` | Debug + tests + unity‑build (`CMAKE_UNITY_BUILD_BATCH_SIZE=16`) + ccache auto‑wired by `CMakeLists.txt` |
| Daily orchestrator | `./tools/build_dev.sh` | build + labelled fast tests + safe temporary-artifact cleanup |

`./tools/build_dev.sh` is the recommended day‑to‑day entry point. Use
`./build-fast.sh` for focused targets and debugging.

---

## TL;DR

```bash
# First run (one per machine): configure + bootstrap ccache + first build
#   → baseline cold: 3–60 min depending on host
#     (warm vcpkg: 3–15 min; fresh vcpkg manifest install: up to 30–60 min)
./build-fast.sh

# Day‑to‑day incremental rebuild and fast tests
./tools/build_dev.sh

# Or pick a concrete target (still under 30 s warm)
./build-fast.sh cli         # ~3 s (just relink)
./build-fast.sh scene       # ~5–10 s
./build-fast.sh test '<pattern>'

# Turbo path (CLI only, no tests/content, batch size 32): even faster cold
./build-fast.sh turbo
```

No manual ccache configuration required — `build-fast.sh` bootstraps it on
the first invocation.

---

## How the <30s number is achieved

Two pieces work together. Both are **transparent** (zero flags to remember),
both are **idempotent on re-runs**.

### 1. `ccache` — persistent, project-local by default, ~20 GiB

Auto‑bootstrapped to `.ccache/ccache.conf` on first run. Set `CCACHE_DIR`
explicitly to use an external or CI-owned cache; explicit caches are never
modified by the wrapper:

```
max_size = 20G
sloppiness = include_file_mtime,include_file_ctime,time_macros,pch_defines,file_macro
compression = true
compression_level = 6
hash_dir = false
cache_dir_levels = 3
temporary_dir = ${CCACHE_DIR}/tmp
```

| Sloppiness flag | Effect |
|---|---|
| `pch_defines` | PCH headers with different preprocessor defines treated as one |
| `time_macros` | `__TIME__`/`__DATE__`/`__TIMESTAMP__` ignored in hash |
| `file_macro`   | `__FILE__` ignored in hash |
| `include_file_mtime` / `include_file_ctime` | include‑file mtime/ctime flicker ignored → more cache hits in day‑to‑day `touch` workflows |

Trade‑off: a header whose **content** changes without its mtime updating
(e.g. `git checkout` of an unchanged mtime, or content edit + `touch -d`)
may serve a stale object. Acceptable for dev; CI uses a fresh cache.

`CMAKE_CXX_COMPILER_LAUNCHER=ccache` is automatically set by
`CMakeLists.txt` when the binary is on PATH — no CMake‑side plumbing needed.

### 2. Build directory

`build-fast.sh` defaults the build directory to
`.tmp/chronon-builds/linux-fast-dev` on disk. Set `BUILD_DIR_OVERRIDE` to use
another location. A symlink at `build/chronon/linux-fast-dev` keeps the CMake
binaryDir stable so existing tools resolve it transparently.

### Measured timings on this host

| Scenario | Wall‑clock | ccache hit | Notes |
|---|---|---|---|
| Cold full rebuild (`-z` reset, no unity reuse) | **5 m 27 s** | 0 % | one‑off per day; baseline 5–15 min on other host profiles (see "Cold build" below) |
| No changes → `./tools/build_dev.sh` | **~13 s** | n/a | ninja no‑work + fast-test scan |
| `touch 1 .cpp` → `./build-fast.sh` | **~17 s** | 100 % | sloppiness‑driven hit |
| Touch hot header `src/scene/camera/camera_debug_overlay_panels.hpp` (7 dependents) | **~17 s** | 100 % | sloppiness covers mtime flicker |
| `./build-fast.sh cli` (relink only) | ~3–5 s | n/a | single‑target |

## Cold build (zero hit) — first run on a fresh machine

If you are the **first contributor on a fresh machine** — no ccache entries,
no populated build directory — the first `./build-fast.sh` will compile
**everything** from scratch. This is unavoidable; it just adds minutes.

| Host profile | Approx. wall‑clock (cold) |
|---|---|
A **vcpkg‑cold** row, for a contributor on a truly fresh machine where even
the dependency manifest is not yet installed, sits *above* the table — vcpkg
assembles the whole dependency chain from sources (spdlog, fmt, glm, tbb,
blend2d, freetype+harfbuzz, openexr, …) on the first cmake configure:

| Host profile (vcpkg cold + ccache cold) | Approx. wall‑clock (cold) |
|---|---|
| 4 cores, 8 GB RAM | **30–60 min** |
| 8 cores, 16+ GB RAM | **15–25 min** |
| 16+ cores, 32+ GB RAM | **10–20 min** |

These bounds collapse to the table above once vcpkg has warmed up
(`vcpkg_installed/` is populated), which is the state a contributor on
day 2+ experiences.

The numbers below are measured **after** vcpkg is already warm:

| Host profile (`vcpkg_installed/` already warm, ccache cold, on this host) | Approx. wall‑clock (cold) |
|---|---|
| 4 cores, 8 GB RAM, SATA SSD | **10–15 min** |
| 8 cores, 16+ GB RAM, fast local disk | **5–7 min** |
| 16+ cores, 32+ GB RAM, fast local disk | **3–5 min** |
| 8 cores, 16+ GB RAM, fast local disk | **5–7 min** |
| 16+ cores, 32+ GB RAM, fast local disk | **3–5 min** |

Measured on this 8‑core / 22 GB host: **5 m 27 s** from a
`ccache -C` reset.

What dominates the cold build, in order of impact:

1. **vcpkg manifest install** — one‑time fetch + compile of dependencies
   (spdlog, fmt, glm, tbb, blend2d, freetype+harfbuzz, openexr, …).
   Dominates fresh machines; cached in `vcpkg_installed/` thereafter.
2. **CMake configure** — fast (single‑digit seconds) once vcpkg is warm.
3. **Ninja invocation** — single `cmake --build` against the `linux-fast-dev`
   preset (Debug, unity build with `CMAKE_UNITY_BUILD_BATCH_SIZE=16`).
4. **Relink** of `chronon3d_cli` + tests (mold, fast on the local build disk).

Useful flag during a cold run on slow disks:

```bash
JOBS=$(($(nproc) / 2)) ./build-fast.sh    # halve parallelism to reduce IO thrash
```

After the first cold run, ccache starts firing and subsequent runs land in
13–17 s.

---

## Environment knobs

All optional. Listed in `./build-fast.sh --help` as well.

| Variable | Default | Purpose |
|---|---|---|
| `JOBS` | `nproc` | parallel ninja jobs (`JOBS=8 ./build-fast.sh`) |
| `CCACHE_DIR` | `<repo>/.ccache` | explicit values are never modified |
| `BUILD_DIR_OVERRIDE` | `<repo>/.tmp/chronon-builds/linux-fast-dev` | override the default build directory |

---

## Cheatsheet — `./build-fast.sh <command> [<args>]`

| Command | Builds | Runs tests |
|---|---|---|
| `./build-fast.sh` | `chronon3d_dev_fast` (CLI + fast tests) | — |
| `./build-fast.sh cli` | `chronon3d_cli` | — |
| `./build-fast.sh scene` | `chronon3d_scene` | — |
| `./build-fast.sh ext` | `chronon3d_extension` | — |
| `./build-fast.sh test '<pattern>'` | + core test binary | doctest pattern |
| `./build-fast.sh scene-test '<pat>'` | + scene test binary | doctest pattern |
| `./build-fast.sh cli-test '<pat>'` | + CLI test binary | doctest pattern |
| `./build-fast.sh ctest [filter]` | depends on filter | whole ctest run |
| `./build-fast.sh turbo` | CLI only (linux-turbo preset) | — |
| `./build-fast.sh turbo-inc <group>` | single CLI group lib + relink | — |

`<group>` for `turbo-inc`: `dev | render | video | telemetry | bench | core`.

Use:

- `./build-fast.sh` for normal dev loops
- `./build-fast.sh test '<pattern>'` when only the test binary is what you care about
- `./build-fast.sh turbo-inc video` for snappy CLI iteration (sub‑second when only one group touched)

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| First build takes many minutes | cold ccache + cold build dir | expected; see "Cold build (zero hit)" above for the host‑profile range (3–60 min depending on vcpkg state). After the first warm‑vcpkg run, drops to 13–17 s |
| Cold run feels stuck on a slow disk | IO thrash from full parallelism | try `JOBS=$(($(nproc) / 2)) ./build-fast.sh` to halve parallelism |
| `./build-fast.sh` says the build disk is full | selected build directory is too small | use `BUILD_DIR_OVERRIDE=/path/on/ssd` |
| `ccache -s` shows MISS where you expected HIT | sloppiness not in effect | confirm `<repo>/.ccache/ccache.conf` matches above; explicit caches are not modified |
| `--report` runs ignore the build cache | expected — each run re‑renders | this is a render‑time concern, not a build‑time concern |

---

## When NOT to use this workflow

- **Release builds.** Use `cmake --preset linux-release-validation` — those runs are slower on purpose (no development-cache sloppiness) and produce deterministic binaries meant for shipping.
- **CI.** CI uses ephemeral, fresh ccache dirs and is intended to compile from scratch. The auto‑bootstrap detects a non‑default `CCACHE_DIR` and steps aside.
- **`gcc`/`clang` ABI‑breaking header changes.** Touch the canonical headers (`feature_zone/*` or `core/*`) and the cache may serve a stale object — run `ccache -C` to invalidate, then rebuild.
