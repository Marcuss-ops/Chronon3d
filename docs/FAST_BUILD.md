# Fast Build — Sub‑30s Incremental Workflow

> Goal: a typical **incremental rebuild** of `chronon3d_dev_fast` (CLI + fast
> tests) lands **well under 30 s**, often around **13–17 s** on Linux with
> ccache warmed up and a tmpfs build dir. Cold first‑build still takes
> several minutes — that is normal and expected.

This page covers the **operator workflow** that backs those numbers. The
implementation lives in two places:

| Component | Where | What it does |
|---|---|---|
| Wrapper script | `./build-fast.sh` | resolves build dir (`/tmp` ↔ on‑disk), bootstraps ccache, invokes Ninja |
| CMake presets | `CMakePresets.json::linux-fast-dev` | Debug + tests + unity‑build (`CMAKE_UNITY_BUILD_BATCH_SIZE=16`) + ccache auto‑wired by `CMakeLists.txt` |

`./build-fast.sh` is the only entry point a day‑to‑day developer needs.

---

## TL;DR

```bash
# First run (one per machine): configure + bootstrap ccache + first build (~5 min cold)
./build-fast.sh

# Day‑to‑day incremental rebuild (~13–17 s with warm ccache + tmpfs)
./build-fast.sh

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

### 1. `ccache` — persistent, SSD‑backed, ~20 GiB

Auto‑bootstrapped to `~/.ccache/ccache.conf` on first run:

```
max_size = 20G
sloppiness = include_file_mtime,include_file_ctime,time_macros,pch_defines,file_macro
compression = true
compression_level = 6
hash_dir = false
cache_dir_levels = 3
temporary_dir = /tmp/ccache-tmp
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

### 2. Build dir on `tmpfs` (RAM disk)

Resolved by `resolve_build_dir()` inside `build-fast.sh`:

1. If `$ROOT_DIR/build/chronon/<preset>` is already a symlink → use it (no‑op).
2. Else if `/tmp` has at least `CHRONON3D_TMPFS_MIN_GB` GiB free (default **16 GiB**) → move the legacy on‑disk slot into a tmpfs path and install an atomic symlink (`ln ... .ln.<pid> && mv -T`).
3. Else → fall back to on‑disk location (with a clear warning).

Result: `cmake --build build/chronon/linux-fast-dev` resolves through the
symlink to `/tmp/chronon-builds/linux-fast-dev` and reads/writes RAM. No
CMake reconfigure is needed.

The legacy on‑disk slot is **never silently destroyed** — if it is a real
populated directory, `build-fast.sh` refuses the swap and prints:

```
⚠️  /…/build/chronon/<preset> is a populated directory; not touching it.
    Remove/rename it, or set BUILD_DIR_OVERRIDE to skip auto-detect.
```

### Measured timings on this host

| Scenario | Wall‑clock | ccache hit | Notes |
|---|---|---|---|
| Cold full rebuild (`-z` reset, no unity reuse) | ~5 m 27 s | 0 % | acceptable; one‑off per day |
| No changes → `./build-fast.sh` | **~13 s** | n/a | ninja no‑work + dep scan |
| `touch 1 .cpp` → `./build-fast.sh` | **~17 s** | 100 % | sloppiness‑driven hit |
| Touch hot header `src/scene/camera/camera_debug_overlay_panels.hpp` (7 dependents) | **~17 s** | 100 % | sloppiness covers mtime flicker |
| `./build-fast.sh cli` (relink only) | ~3–5 s | n/a | single‑target |

---

## Environment knobs

All optional. Listed in `./build-fast.sh --help` as well.

| Variable | Default | Purpose |
|---|---|---|
| `JOBS` | `nproc` | parallel ninja jobs (`JOBS=8 ./build-fast.sh`) |
| `CCACHE_DIR` | `~/.ccache` | overridable for isolated CI caches (bootstrap is **skipped** when CCACHE_DIR is set to a non‑default value, to avoid clobbering a shared cache) |
| `BUILD_DIR_OVERRIDE` | unset | skip tmpfs auto‑detect; use exactly this path |
| `CHRONON3D_TMPFS_MIN_GB` | `16` | minimum free GiB in `/tmp` to qualify for the tmpfs path |

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
| First build takes many minutes | cold ccache + cold tmpfs dir | expected; ccache hits accumulate across runs |
| `./build-fast.sh` says `/tmp` has too little free space | `CHRONON3D_TMPFS_MIN_GB` too high OR another tmpfs consumer | drop the threshold or use `BUILD_DIR_OVERRIDE=/path/on/ssd` |
| ++`⚠️ … is a populated directory; not touching it.` | legacy on‑disk slot is real | `rm -rf build/chronon/linux-fast-dev` (only if empty of artifacts you need) or `BUILD_DIR_OVERRIDE=/tmp/chronon-builds/linux-fast-dev` |
| `ccache -s` shows MISS where you expected HIT | sloppiness not in effect | confirm `~/.ccache/ccache.conf` matches above; for shared CI caches the bootstrap is intentionally skipped |
| `--report` runs ignore the build cache | expected — each run re‑renders | this is a render‑time concern, not a build‑time concern |

---

## When NOT to use this workflow

- **Release builds.** Use `cmake --preset linux-release` (or `linux-release-full`) — those runs are slower on purpose (no sloppiness, no tmpfs) and produce deterministic binaries meant for shipping.
- **CI.** CI uses ephemeral, fresh ccache dirs and is intended to compile from scratch. The auto‑bootstrap detects a non‑default `CCACHE_DIR` and steps aside.
- **`gcc`/`clang` ABI‑breaking header changes.** Touch the canonical headers (`feature_zone/*` or `core/*`) and the cache may serve a stale object — run `ccache -C` to invalidate, then rebuild.
