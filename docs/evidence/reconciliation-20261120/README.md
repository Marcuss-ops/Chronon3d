# Reconciliation 2026-11-20 — HEAD, ticket inventory, build

This checkpoint records what was verified on this host at
`Chronon3d@78a7473b7` (`feat: improve scene materialization`, 2026-11-19) with a
clean working tree. It does not promote any external gate to PASS.

## Commands and results

| Check | Command | Result |
|---|---|---|
| HEAD / tree | `git rev-parse HEAD`, `git status --short` | `78a7473b7`, clean |
| Release CLI build | `cmake --build build/chronon/linux-video-release --target chronon3d_cli -j8` | **exit 0**, 100.9 s, max RSS 1.7 GiB |
| Test tree build | `cmake --build build/chronon/linux-video-test -j8` | **exit 0**, 756 edges, 0 errors, peak 19.9 GiB free |
| CTest suite | `ctest --test-dir build/chronon/linux-video-test -j4 --output-on-failure` | **92/94 passed, 2 failed** (`chronon3d_deterministic_tests`, `chronon3d_golden_e2e_tests`) |
| Deterministic rerun with Release CLI | `CHRONON3D_CLI_BINARY=<release>/chronon3d_cli chronon3d_deterministic_tests` | 93/94 cases; the single red is a Vulkan object-lifetime validation error (see below) |
| Release CLI encoder probe | render `02_packshot_2p5d` with `--ffmpeg-mode pipe` | **PASS**, libx264 encodes |
| Test-lane encoder probe | same command with the test-lane CLI | **FAIL**, `create_video_sink() returned nullptr` |
| RAM guard | `MemAvailable` before build | 21.6 GiB (guard: ≥20 GiB for `-j12`); used `-j8`, minimum observed 11.6 GiB free |
| Native effect routes | `grep kNativeRoutes include/chronon3d/render_graph/effect_gpu_capability.hpp` | **65** routes (`std::array<NativeRoute, 65>`) |
| TODO/FIXME census | `grep -rniE '\b(todo|fixme)\b' src include apps` | **0** occurrences |
| Build lanes | `CMakeCache.txt` | release: tests OFF; test: tests ON, 94 CTest entries; both SVG/content/OCIO/motion-adapter **OFF** |
| Host GPU | `nvidia-smi` | NVIDIA RTX A4000 present |
| Content pack | `ls content` | **absent** (`CHRONON3D_BUILD_CONTENT=OFF`) |
| Archive move | `git mv` of 38 closed sheets + link rewrite | 55 sheets in `docs/tickets/`, 83 in `archive/` |
| Link regression | resolved-link comparison against `HEAD` docs | **0 new broken internal links** (43 pre-existing, 18 resolved) |

## Archiving

Closed sheets moved to `docs/tickets/archive/` per `docs/INDEX.md` §"Ticket:
aperto vs archiviato". Links were rewritten by resolving each relative target
against its final directory, not by string substitution. Three closed sheets
with a live residual were deliberately left in `docs/tickets/`:
`TICKET-REGISTRY-HARNESS-VK-INSTANCES` (NVIDIA ICD lane still blocked),
`TICKET-SCALE-P0-ASSET-VRAM-GOP` (one corpus criterion open) and
`TICKET-SVG-TEXT-PARITY-REMAINING` (P2 open).

## Suite failures — attribution

**1. `chronon3d_deterministic_tests` — RESOLVED 2026-11-20.** The validation
red was a missing `vkDestroyDescriptorSetLayout` for
`inline_mesh_bin_descriptor_layout` (created at
`vulkan_backend_lifecycle_layouts_private.cpp:150`, absent from the teardown
block). After adding it, the Release CLI rebuilds (`5/5`, 50 s) and the test
passes **94/94 cases, 11,797 assertions, 0 validation errors, rc 0**. The
lane-configuration part remains: with `CHRONON3D_ENABLE_NATIVE_FFMPEG=OFF` the
test-lane CLI still cannot open an encoder, so the case only runs with
`CHRONON3D_CLI_BINARY` pointing at an encoder-capable CLI.

Original attribution: **1. `chronon3d_deterministic_tests` (lane configuration).** The test lane has
`CHRONON3D_ENABLE_NATIVE_FFMPEG=OFF` (release has `ON`), so its own CLI cannot
open an encoder: the same plan encodes with the Release CLI and fails with the
test-lane CLI. Re-running the suite binary with
`CHRONON3D_CLI_BINARY=<release CLI>` removes that red and leaves one real
failure: `tests/deterministic/test_text_warm_cold.cpp:279` observes
`VUID-vkDestroyDevice-device-00378` — a `VkDescriptorSetLayout` is not
destroyed before the device. HEAD `78a7473b7` touches only compiler/
materializer files, so the leak predates it and comes from the Vulkan perf
commits (`ff949336a`, `588f2c14f`, `18077e3ae`, `c33bbe33f`). It is not covered
by the SyncVal scope of `TICKET-RENDER-STACK-HARDENING`.

**2. `chronon3d_golden_e2e_tests` — RESOLVED 2026-11-20.** Frame 0 of all
three plans was rendered with the Release CLI and reviewed: the visible delta
is confined to the image panels of 02/03. It comes from `78a7473b7`, which
replaced the `static_cast` between `LayerFitMode` and `FitMode` with an
explicit mapping; the enums declare their values in different orders, so
`"fit": "stretch"` had been behaving as cover (cropping) and now stretches.
The text-only plan is unaffected, which matches the observed pattern. The
manifest was rebaselined through `run_golden_e2e.py --update-goldens`, then
the `backend/date/host/renderer_sha/review` metadata was restored (the runner
writes only `schema/generator/scenes`). `ctest -R chronon3d_golden_e2e_tests`
now passes.

Original analysis: the goldens were rebaselined
on 2026-11-16 at `30b4b721a` with `backend: software`
(`goldens.v1.json`); the runner always passes `--backend software`, so the
comparison is like-for-like. At HEAD, `02_packshot_2p5d` hashes to
`2ef9c9b7ed3aae8f` (golden `38d28c27625c0e4b`) and `03_mixed_mesh` to
`0cf034343ca695d4` (golden `eb961e58b33b6167`), while `01_editorial_text` still
matches. HEAD changes `scene_materializer.cpp` and `scene_lowering.cpp`, which
matches the affected scenes (2.5D packshot and mixed mesh) and not the text
scene. The drift needs a reviewed rebaseline, not a blind `--update-goldens`.

Both items are recorded in `docs/tickets/TICKET-SUITE-RED-20261120.md`.

## Limitations

- The suite was run only in `linux-video-test`, which compiles neither SVG nor
  content nor OCIO gates; those lanes are not covered by this result.
- No same-SHA CI run exists for `78a7473b7`; Actions were not queried.
- The 2026-11-16 SVG gate results cannot be reproduced from these build trees
  (`CHRONON3D_ENABLE_SVG=OFF` in both lanes).
