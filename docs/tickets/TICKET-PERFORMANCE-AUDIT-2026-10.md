# Performance audit — October 2026

Status: OPEN — local Chronon3d worktree audit; remaining same-SHA CI, corpus,
benchmark and native hardware gates are still pending.

## Goal

Remove confirmed resource/measurement defects and bounded hot-path overhead,
without weakening residency, aliasing, pixel correctness, strict GPU policy or
memory admission. No percentage speedup is accepted without before/after runs.
No push/deployment is part of this work.

## Acceptance checklist

- [x] Record pre-existing diff and build configuration; Release Vulkan/CUDA/native
      FFmpeg and ccache are available. Build cap is 20 jobs; monitor RAM.
- [x] Preserve fractional cache/dirty timings before integer-ms conversion;
      regression tests cover concurrent samples, reset and copy.
- [x] Close external memory/semaphore FDs on every failed export/import;
      retain transferred descriptors and destroy partially imported CUDA objects.
- [x] Pipeline cache validates bounded length/vendor/device/UUID, includes driver
      and embedded shader fingerprint in filename, tolerates storage failures,
      and publishes complete files atomically under concurrent writers.
- [ ] Reduce binding scratch allocations without changing carrier lifetime,
      collisions, placement or output; record representative before/after data.
- [x] Evaluate dirty-region coalescing; no algorithm change adopted without representative runtime evidence.
- [x] Verify software effect scratch does not create framebuffer allocations after warm-up.
- [ ] Measure available native CLI lifecycle/handoff and verify output frames and
      zero-copy counters; do not remove fences or admission guards by inference.
- [ ] Rebuild final touched targets and run relevant C++/architecture/tool gates.
- [ ] Record final results and exact remaining blockers.

## Evidence location

Ignored local logs/artifacts: `.tmp/performance-audit/`. Original tracked diff
snapshot: `/tmp/chronon3d-audit-preexisting-20261004.diff`; original untracked
path census is saved alongside it. These are not release artifacts.

## Verified checkpoint

- Focused existing C++ test sources: 12/12 cases, 66/66 assertions PASS;
  repeated under ASan/UBSan with leak detection: PASS.
- Architecture gate: 206/206 PASS on the checked worktree (rerun final).
- Common performance gate, steady-state allocation gate, shader-layout contract,
  GPU corpus manifest, admission source contracts and memory-gate self-test PASS.
- Pre-optimization native CLI reference: grid and `black_glow_inter`, 30 frames
  each, MP4 + sidecar PASS; grid zero-copy: 19/19 PASS. These runs overlapped a
  CPU build, so they are smoke/lifecycle evidence, not a speedup reference.
- Full suite build exposed FFmpeg profile naming incompatibility in existing
  production and packet-assembler code. Both now use the installed
  `AV_PROFILE_H264_*` constants, preserving the profile values and assertions.
- Existing export-fence wait counters were never populated. Their production
  wait sites are now instrumented; no fence or synchronization policy removed.

## Scope boundaries

Broad external B00–B11/text content-pack certification, remote runner dispatch,
and daemon production-worker measurements are separate acceptance gates. Their
absence cannot be represented as PASS. Persistent-job resources, fence removal,
sparse native video and job concurrency changes require measured correctness
and memory budgets, not merely successful compilation.

## Audit finding reconciliation — 2026-10-05

Applied locally on `main@b67a750e6` (not pushed):

- Fixed centered rect/rounded-rect stroke expansion in software and Vulkan,
  and full-width inside-circle stroke geometry; direct authored-distance
  assertions added.
- `JobSession` now owns an initialized echo-history state and clears it on
  destruction. The session contract states that the borrowed runtime/backend/
  registry outlive it; a surface-release test covers the valid lifetime order.
- Native Echo reserves its complete per-key history budget before allocating
  frame temporaries. Over-budget keys are rejected to software at admission;
  accepted keys reserve space and no longer evict one another. Added cap/reset
  assertions. Three Echo scratch surfaces now persist per history key and frame
  slot, with a separate 128 MiB JobSession cap and observable software fallback
  on scratch admission or creation failure. Only the output surface is created
  per invocation; the temporary-handle cleanup tracker uses fixed storage.
- Empty animation tracks no longer overwrite static layer opacity. Structural
  program fingerprints now mix each count as a separate ordered field instead
  of XORing overlapping bit ranges. Added focused regression cases.
- The reported per-frame `std::string` copies and `find_if` scans in
  `TypedProgramSampler` are already absent at this SHA: image/video payloads
  use `std::string_view`, and `StaticLayerProgram` stores resolved text and
  animation indices. The legacy `render_scene(Scene, Camera2_5D)` clone is
  deprecated and is not the typed `render_program` hot path. Static layer
  matrices are now lowered once; the frame sampler rebuilds quaternion/matrix
  state only for layers with non-empty transform animation tracks. Added an
  opacity-only sampling contract test; the compiler boundary suite passes
  (1/1, including 20 assertions for static-matrix reuse).
- Replaced the Vulkan vignette's signed-integer square sum with floating-point
  vector length.
- Removed `OverlayTemplateCache`, which had no production consumer, from the
  runtime, CMake source list and cache taxonomy.
- Reconciled two audit items against current code: plan audio mixing is already
  implemented (`TICKET-AUDIO-RENDER-PLAN` is COMPLETE), and the Vulkan keying
  transform already requests straight-alpha processing; its parity fixture
  includes premultiplied inputs with alpha 0.5 and nonzero softness.
- The `record_text_batch`/`text_batch.comp` path is already demolished at this
  SHA (`GpuCommandPassKind::TextBatch` value 10 is reserved, with no producer
  or shader); removed the generator's stale example reference.
- The encoder mutex could not be made non-recursive by changing its type alone:
  `close()` called the public, locking `encoder_target_contract()` while already
  holding the lock, and CUDA drain helpers redundantly acquired it from locked
  callers. Split the lock-free internal contract refresh from its public locked
  wrapper, made the drain helper rely on its locked callers, and changed the
  authority to `std::mutex`. The direct hybrid mux regression now passes, which
  exercises the borrowed-container close path that previously deadlocked.
- The exporter logs `sidecar_write_ms` and `session_teardown_ms`; the sidecar
  completion timestamp now crosses the exporter wrapper, and the RenderJob
  logs `post_sidecar_to_receipt_ms`, `receipt_build_verify_ms`, and
  `receipt_file_write_ms`. The CLI integration case verifies timestamp
  propagation. Fresh daemon samples are still needed to attribute the reported
  1.28 s using these fields.
- Camera 2.5D projection is active in the software path exercised by
  `camera_25d_tests.cpp`; the reported lacuna was disabled pixel assertions.
  Re-enabled absolute/silhouette assertions for camera translation, near/far
  scale, X/Y foreshortening, 90-degree collapse and depth ordering. The focused
  camera cases pass (8/8, 32 assertions).
- Motion-blur sample resources now live in a `JobSession` workspace indexed by
  the active frame-slot ID. Each slot reuses its sample value caches, compiled
  graph caches, transform scratch buffers, framebuffer pools and isolated
  sample sessions. Frame-start clears value cache and Echo history while
  retaining pool/scratch allocations. Temporal-only sessions use one 256 KiB
  arena slot (ordinary jobs retain three 4 MiB slots); at 64 samples and three
  frame slots this caps persistent temporal-session arena storage at 48 MiB
  instead of 768 MiB. A regression test checks cross-slot isolation, workspace
  identity across reuse, and the temporal arena capacity. Core, renderer and
  scene suites pass (3/3), including temporal-accumulation integration. No
  before/after timing claim is made yet.
- Fixed the local FFmpeg profile macro mismatch in production and its tests by
  using the installed `AV_PROFILE_H264_*` constants. The `chronon3d_cli_tests`
  target now builds against the configured FFmpeg headers.
- The 2026-10-05 same-SHA GitHub Actions runs `37313378103` and `37313378100`
  still fail before any job step (`steps: []`); runner billing remains outside
  this source checkout.
- The current host exposes only Mesa llvmpipe as Vulkan GPU0 and has no
  `nvidia-smi`; it cannot provide the requested real NVIDIA hardware
  certification for native keying/Echo.

Added `tools/fixtures/vram-calibration-4k-overlay.plan.json` as a 3840×2160,
48-frame calibration shape and validated it with the production CLI parser
(`validate --plan`, PASS; content digest
`4df908b0d4578545e27e6b1c2b6d36045500bed49f41ff44110bc4fef36435af`). The
external VRAM calibration suite has not consumed or measured it yet.

The suspected Vulkan Keying double-premultiply is not reproduced. A dedicated
pixel-oracle case uses premultiplied source pixels with alpha 0.5/0.25, a soft
luma threshold and partial keep alpha; strict Vulkan matches the software
processor within 1.49e-7 on llvmpipe (8 channel assertions). The lowering selects
straight-alpha processing, then premultiplies once by the resulting alpha.

Still open from the supplied audit: daemon batching/concurrency and lifecycle
measurement (instrumented, but fresh 1.28 s attribution remains to be collected);
representative before/after motion-blur timing; the remaining required Echo trail
clear and per-frame output-surface allocation; unsupported strict-Vulkan effect
routes; WBH/toolchain protocol and 4K VRAM suite inclusion/calibration; and
hardware certification for GPU keying/Echo. The historical
`bug6-wip-check` entry is absent from the current stash list and its patch is
not present among reachable or unreachable commits, so it cannot be applied
verbatim; resolver behavior still needs a fresh contract review before that
work is reconstructed. The regenerated Debug/video build completed for
`chronon3d_core_tests`, `chronon3d_renderer_core_tests` and
`chronon3d_backend_registry_tests`. CTest passed all three suites (3/3), including
the absolute software stroke pixels and native Echo reservation/destruction
cases. The latest registry build and suite pass (1/1), including Echo scratch
surface reuse, cross-slot isolation, release-on-clear and pixel parity (94
focused assertions). The CLI target also builds, the direct hybrid mux test passes (1 case,
194 assertions), and the serialized CLI CTest rerun passes (1/1; 241 test cases,
5387 assertions). An initial CLI run overlapped the mux fixture and failed two
assertions; the standalone full suite and isolated CTest rerun both passed. The
scene suite passes (1/1) with restored Camera 2.5D pixel assertions.
The architecture gate passes 206/206 on this exact worktree, and
`git diff --check` is clean.
The full render-graph CTest suite now passes (1/1, 39.79 s) after adding
TemperatureTint to strict Vulkan native color dispatch. Its capability census
is 19/47 native routes (28 explicitly unsupported); focused parity covers
mixed/clamped parameters, identity and clipping, and the full registry suite
passes on local llvmpipe. The architecture gate was rerun after this addition
and remains 206/206.
The latest local effect routes are LiftGammaGain, Offset, Noise and Checkerboard (23/47
native, 24 explicitly unsupported). Noise reproduces the software SplitMix64
pixel hash with exact seed/frame bit transport, and parity covers animated and
static modes, channel mode, size, tone response, clip and no-op contracts.
Its focused parity test passes (10 assertions; maximum error 5.96046e-8), and
ordered AdjustmentNode composition passes (130 assertions). LiftGammaGain's
mode is carried through Vulkan push constants and separated from Vignette/Mosaic
shader dispatch; parity covers alpha, HDR, clipping and software parameter
clamps. Offset covers all edge/filter combinations plus integer translation
and the zero-offset no-op.
The focused parity cases and strict ordered adjustment composition pass on
local llvmpipe. Backend registry CTest passes (1/1, 18.96 s) and render-graph
CTest passes (1/1, 39.07 s) after Offset integration.
After Noise integration, backend registry CTest passes (1/1, 19.72 s),
render-graph CTest passes (1/1, 40.86 s), architecture gate passes 206/206,
and `git diff --check` is clean.
After the latest sampler and Echo workspace changes, the core, compiler
boundary and backend registry CTest suites each pass (1/1); architecture remains
206/206.
Native Echo now clears only its per-slot trail accumulator each invocation;
current, composed and output surfaces are fully overwritten by transform/copy
passes. This removes three transparent initialization dispatches per frame.
The four Echo-related parity/history/ownership cases pass (215 assertions), and
the full backend registry CTest passes (1/1, 19.81 s). No wall-clock speedup is
claimed; output-surface allocation and the necessary trail clear remain.
The strict Vulkan census has advanced to 23/47 native with the Checkerboard
generator; focused parity, ordered composition, backend registry CTest and
render-graph CTest pass on llvmpipe. The audited `bug6-wip-check` stash entry
is absent from the current stash list (only stash@{0} through stash@{2} exist),
so its reported 116-line placement test patch cannot be recovered from that
reference in this checkout.
The steady-state allocation script exits successfully but explicitly reports
its certified baseline as historical (`8bdf0f807`, expected `b67a750e6597`);
that result is not current-revision allocation evidence and needs a fresh
measurement before the performance gate can close.
The same-SHA remote CI runs still have no
executed steps, and hardware certification remains outstanding. No performance
speedup is claimed without representative before/after measurement. This ticket
stays ACTIVE.

## CPU verification — 2026-10-11

- `chronon3d_core_tests` was rebuilt in `linux-fast-dev`; the focused
  `PreparedRenderJob effect scratch prevents steady-state framebuffer
  allocations` case passes **37/37 assertions**. It compares a rendered
  `RadialBlur` scene with its no-effect baseline, then checks that both
  `framebuffer_allocations` and `framebuffer_bytes_allocated` remain unchanged
  for 30 frames after two warm-up frames.
- `chronon3d_effects_tests` was rebuilt and its registered CTest passes **1/1**.
- The preset has `CHRONON3D_ENABLE_VULKAN=OFF`; these checks certify the CPU
  software route only. Dirty-region coalescing, representative before/after
  binding-scratch measurements, current same-SHA CI and the native GPU lanes
  remain open.

## Vulkan carrier binding measurement — 2026-10-11

- `plan_carrier_slots_scratch_` now reserves `carriers.size()` before collecting
  the known carrier slots. On the first multi-carrier plan this replaces
  repeated vector growth with one capacity allocation; retained capacity means
  later bindings reuse it. Carrier ordering, slot assignment and lifetime logic
  are unchanged.
- Added a two-initialized-carrier alias regression case. It verifies that the
  planner aliases input/output, then executes the two-pass transform 64 times
  and checks output pixels each time. It passes 327/327 assertions.
- Before/after binding-only benchmark, five process runs × 64 iterations each:
  baseline median **35.39 µs/call**, reserved median **34.32 µs/call**. Individual
  runs overlap substantially; this is not evidence of a measurable speedup.
  The change is retained for the bounded first-use allocation reduction, not
  for a claimed latency gain.
- Both versions ran on Vulkan `llvmpipe` (`PHYSICAL_DEVICE_TYPE_CPU`), so this
  validates Vulkan behavior and CPU-driver timing only; NVIDIA hardware
  certification remains open. The checklist item remains open pending a
  representative hardware run and final audit gates.

## Dirty-region coalescing review — 2026-10-11

- `ExecutionResolver::coalesce_dirty_regions()` scans the tile grid, reduces
  each dirty row to its first/last dirty tile, then merges adjacent rows only
  when their horizontal bounds match. The focused regression passes **1/1 case,
  9/9 assertions** and checks the merged rectangle bounds.
- Merging rows with shifted spans could lower region count but would expand the
  bounding boxes and render clean tiles. No representative frame/tile timing
  data in this worktree supports that tradeoff, so the current algorithm is
  retained. This closes the review decision only; it makes no performance claim
  and does not close the ticket's separate lifecycle, hardware, or final-gate
  criteria.

## Current worktree architecture gate — 2026-10-11

`python3 tools/check_architecture.py` completed on the current dirty worktree:
**206/206 architecture rules PASS**. This is a current local gate result; it
does not substitute for the remaining C++ suites, GPU/CI evidence, or clean-SHA
certification.

## Native CLI lifecycle gate attempt — 2026-10-11

- The available `linux-fast-dev` CLI can render `DarkGridBackground` to a
  1920×1080 PNG, but its execution report records build commit `96afb456c`,
  while the current checkout is `e8e06f5b3`. That run is stale and is excluded
  from current-SHA evidence. The still-image route also warns that it is not a
  text pixel-truth surface.
- The `linux-video-test` CLI target is not built. Building it failed while
  archiving `libchronon3d_cli_core.a` with `No space left on device`; a retry
  with `-j1` failed at the same archive step, so parallel disk pressure was not
  the cause. The failed archiver left a 1.0 GiB partial archive that is not a
  valid build result. The filesystem reports 2.9 GiB free and the existing
  fast-dev build tree uses 29 GiB. No pre-existing build artifacts were
  removed. Native video lifecycle, output-frame, and zero-copy-counter gates
  therefore remain NOT RUN on the current SHA.
