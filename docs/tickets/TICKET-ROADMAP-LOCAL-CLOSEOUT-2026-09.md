# Performance / quality roadmap — repository closeout

Date: 2026-09-25

This ticket records the repository-local acceptance boundary for the roadmap
review. It does not claim same-SHA GPU certification, performance improvement,
or external corpus coverage. The planned local changes and audits are recorded
below, but the Vulkan shape-stroke path now present in the working tree is an
unverified implementation candidate: stroke parity and a current-tree build/test
are not complete. Acceptance items that require pixel parity, external
hardware, assets, or remote CI remain open below.

## Completed locally in source

- The cached GPU text route's per-frame `[text-route]` INFO record is gated by
  `RenderGraphPolicy::diagnostics_enabled`; normal native/software/reject route
  counters and route selection are unchanged. `test_gpu_text_cached_policy.cpp`
  covers both diagnostics states and the reject counter.
- Tiled text uses the production `runtime::TextRunDynamic` record (128 bytes).
  The backend and Vulkan canary assert its size/alignment. The shader layout
  contract checks the C++ field sequence, std430 size, both tiled shader
  declarations, and (when `glslangValidator` is installed) each compiled
  `ArrayStride`.
- Strict native RGB sparse rendering remains deliberately full-frame:
  `ExecutionResolver` clears previous/dirty-surface handoffs with reason
  `native_gpu_disables_sparse_tiles`. The regression contract is in
  `test_frame_delta_compiler.cpp`. `ExecutionResolver` has no persisted
  per-slot GPU surface version/history to safely apply accumulated deltas to a
  reused GPU image; do not remove this gate until that ownership contract is
  provided by the existing slot authority.
- Sparse YUV was also only selected by the resolver: no scene executor consumed
  `FrameExecutionPath::SparseYuv` or its dirty regions. It now remains
  `FullYuv` with reason `sparse_yuv_executor_unavailable` until a persistent
  YUV destination, region update, and encoder handoff are implemented. The
  regression assertion is in `test_frame_delta_compiler.cpp`; no partial-YUV
  performance is claimed.

## Audited boundaries — no new authority added

- DirectYUV still has one composition capability matrix; tests cover certified,
  full-frame-only, CPU-only, and unsupported feature classes. The video
  resolver returns an explicit plan and the DirectYUV program validates the
  compiled composition. GOP parity and capability suites were audited but not
  rerun in this closeout; there was no concrete repository-local defect to
  change, and no new capability or parallel cache was introduced.
- GOP planning/execution retains one splice authority and one caller-owned mux.
  Existing packet-assembler/GOP direct-mux parity tests cover copied payload,
  timestamps, decoded frame count/duration and audio; executor schedule checks
  reject malformed ranges/ordinals/incompatible copy units. These video suites
  were audited but not rerun here; no source evidence justified changing or
  weakening the existing rules.
- Batch calibration tooling exists and is intentionally sequential across
  shapes/repetitions. Its report explicitly sets
  `authorizes_mutex_removal: false`; the repository documents that a GPU
  multi-shape, allocator-attributed run is a precondition for changing daemon
  concurrency. No concurrency limit is raised here.
- Source sampling is PTS-native, but output encoding is CFR: the export request
  carries a rational frame rate, the native encoder sets one fixed
  `time_base`/`framerate`, and the render loop assigns sequential frame-index
  PTS. Source VFR support does not constitute VFR output support; output VFR is
  not exposed and remains deferred. CFR remains the only/default output mode.

## Stroke parity status — LOCAL PIXEL PARITY; HARDWARE OPEN

The solid Rect, RoundedRect and Circle Vulkan stroke route now has a software
pixel oracle for all three alignments, translated geometry, clipping, fill and
stroke opacity, and uniform scales 0.75x, 1x, 1.25x and 2x. Coverage is computed
in inverse-transformed local coordinates using the same inverse matrix
coefficients as the CPU rasterizer; the local Vulkan test passes 884,916 pixel
channel assertions on llvmpipe. Non-uniform scale, rotation, masks and
per-corner rounded-rectangle strokes remain unsupported and fail closed.

The nine shape/alignment combinations in `test_vulkan_debug_context.cpp` use
the certified solid-stroke subset. On 2026-10-04, `chronon3d_vulkan_debug_context_tests`
also passed after rebuilding; this rerun did not reproduce the previously
reported teardown abort. GPU hardware certification is a separate external
gate.

## Current-tree verification status — VERIFIED (2026-09-25)

The repository tree was rebuilt in full with `-j 12` (`949/949` targets completed without warnings/errors).
Targeted test suites passed at 100%:
- `chronon3d_backend_registry_tests`: PASS
- `chronon3d_render_job_contract_tests`: PASS
- `chronon3d_compositor_tests`: PASS
- `chronon3d_render_graph_tests`: PASS
- `chronon3d_cli_tests`: PASS (including daemon IPC boundary and startup diagnostic assertions)
- Text stroke & MTSDF coverage parity contract (`tests/media/nv12/nv12_equivalence_test.cpp`): PASS across all stroke widths {0.0, 1.5, 3.5, 8.0} and scales {0.25, 0.31, 1.0} with max error <= 1/255.
- Merge conflict markers check (`./tools/check_conflict_markers.sh`): GATE_PASS.

## External gates still blocked

- Same-SHA Release GPU benchmark and FullGraph/DirectYUV corpus run still need
  the configured GPU host/content pack. Existing baseline files are historical
  commit-specific measurements and do not certify this source revision.
- VRAM calibration on 3–5 representative shapes with allocator attribution is
  required before daemon concurrency can be enabled.
- Remote synchronization is not available in this environment; local commits
  do not imply GitHub push/CI completion. Existing local commits remain
  ahead of origin/main.
