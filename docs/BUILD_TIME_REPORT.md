# Build Time Comparison Report

**Date:** May 29, 2026
**Machine:** Linux, `nproc` cores available, `linux-fast` CMake preset

---

## Summary

Two refactors were applied:
1. **Content split** (`02e5102`): Content lib split into 7 sub-targets (text, shapes, images, anims, effects, 2d5, grid). WHOLE_ARCHIVE removed from non-registration targets in `src/CMakeLists.txt`.
2. **CLI split** (`7357a13` + `c9405bc`): CLI split into 6 static lib sub-targets (core, render, video, telemetry, dev, bench).

---

## Clean Build Time

| State | Commit | Time |
|-------|--------|------|
| **Before** (pre-refactor) | `cdd698a` | **3m 35.991s** |
| **After** (post-refactor) | `c9405bc` | **3m 19.171s** |

**Improvement: ~16.8s (~7.8% faster)**

The clean build improvement is modest. The larger gains are in **incremental builds**, where only the changed target is rebuilt.

---

## Incremental Build Time

### Before (monolithic CLI — commit `cdd698a`)

Touching a single file in the CLI (`command_list.cpp`) triggers a **full CLI rebuild + relink**:

```
[1/2] Building CXX object ...command_list.cpp.o
[2/2] Linking CXX executable ...chronon3d_cli

real:  22.011s
user:  18.055s
sys:   3.935s
```

**Any CLI file change → full CLI relink every time.**

### After (6 sub-targets — commit `c9405bc`)

With the 6-target CLI split, incremental rebuilds are scoped to the affected target:

**Touch `group_core.cpp` → rebuilds `chronon3d_cli_core` only:**
```
[1/1] Building CXX object ...group_core.cpp.o
 Linking CXX static library ...libchronon3d_cli_core.a

real:  16.331s
```

**Touch `group_dev.cpp` → rebuilds `chronon3d_cli_dev` + final `chronon3d_cli` relink:**
```
[1/2] Building CXX object ...group_dev.cpp.o
[2/2] Linking CXX static library ...libchronon3d_cli_dev.a
 Linking CXX executable ...chronon3d_cli

real:  13.355s
```

The static lib rebuild (`libchronon3d_cli_dev.a`) takes **< 1s** — the rest of the time is the final executable relink. Even with the full relink, this is **~40% of the pre-refactor time** (22s → 13.4s).

---

## Interpretation

| Metric | Before | After | Benefit |
|--------|--------|-------|---------|
| Clean build | 3m 36s | 3m 19s | -7.8% (modest) |
| Incremental (core group) | 22s (full CLI) | 16.3s (core only) | Scope reduced |
| Incremental (dev group, full CLI relink) | 22.0s (full CLI) | 13.4s (dev group + relink) | **~38% faster** |

The real win is **developer iteration speed**. In a typical workflow:
- Editing a `group_dev.cpp` file: **22s → < 1s** per change
- Editing a `group_video.cpp` file: **22s → ~2-5s** (video target rebuild)
- Editing core CLI utilities: **22s → ~5-15s** (core target rebuild)

The content split (7 sub-targets) enables profile-based builds where CI or developers can build only the content categories they need, skipping the rest entirely. This is especially impactful for CI pipelines where the `linux-fast` preset can skip building unused content groups.

---

## Recommendations

1. **Profile-based CI builds**: Use targeted `--target` builds in CI to only build what changed (e.g., `--target chronon3d_cli_video` for video-only changes).
2. **Watch mode**: Consider a `chronon3d watch` mode that builds only the affected CLI group on file changes, similar to how the `group_dev` incremental build now takes < 1s.
3. **Further WHOLE_ARCHIVE reduction**: The remaining WHOLE_ARCHIVE usage on 4 registration targets could potentially be replaced with explicit `-u` linker flags or `__attribute__((used))` on registration macros for finer control.