# Chronon3d ccache audit — 2026-09-10

## Result

The active shell did not define `CCACHE_DIR`. The active ccache configuration
therefore resolves to the user cache:

- cache directory: `/home/pierone/.cache/ccache`
- configured maximum: `30.0G`
- observed size: `27.28G` (`90.92%`)
- observed storage hit rate: `24.90%`
- cache reset/zeroed: **no**

The 30 GiB cap is already enforced by `/home/pierone/.config/ccache/ccache.conf`.
No statistics or cache entries were removed during this audit. The active
cache is close to its cap; operators should monitor eviction pressure before
raising the limit.

## Project-local policy

`build-fast.sh` continues to use the project-local `.ccache` when the operator
does not provide `CCACHE_DIR`, with a 20 GiB cap and compression enabled. An
explicit `CCACHE_DIR` is never rewritten by the script. CMake presets used by
the checked-in build and CLI gate select `ccache` as both C and C++ launcher.

## Configure-tree check

The stale `build/chronon/linux-video-test/` directory was absent at audit
time, so no useful configure tree was deleted. The active CLI gate was
configured under `.tmp/chronon-builds/linux-fast-dev` through the `ccache`
launcher preset; the compatibility symlink is
`build/chronon/linux-fast-dev`.
