# P2 + P4–P9 audit verification — 2026-11-18

**Scope.** Follow-up to the P1/P3 pass: P2 (DirectYUV pinned host staging
synchronization), P4 (cooperative tile culling), P5 (deferred-release scratch),
P6 (cross-item NVENC session reuse), P7 (CUDA-queue `EAGAIN` handling), P8
(stack timestamp queries) and P9 (OCIO stat on cache miss only).

**Code under test.** `Chronon3d` HEAD `ff949336a`. Build
`build/chronon/linux-video-test`: Vulkan ON, CUDA interop ON, OCIO OFF.
All seven tickets were already `CLOSED`; this pass independently re-checked the
implementation and re-ran the reachable gates. No production code was changed.

**Changes in this pass.** The P2 12-frame async test in
`tests/media/nv12/nv12_equivalence_test.cpp` was still written against the
pre-unification API (`runtime::LayerBatch`, `CudaNv12Compositor::init`,
`PrimitiveKind::Solid`, `dst_rect`, `make_gradient_nv12`) and did not compile;
the target had never been linked. It now uses the current
`GpuLayerBatch`/`LayerInstance` + `CudaLayerResource` API and a real RGBA
overlay. A P4 test was added: 65 sparse layers (one full 64-layer chunk + one)
compared against per-layer draws inside each layer's luma and covering chroma
regions. The full-frame comparison is intentionally not used there because the
other 64 layers legitimately occupy the rest of the frame.

## Build and gates

| Gate | Command / condition | Result |
|---|---|---|
| NV12 target build | `cmake --build build/chronon/linux-video-test --target chronon3d_nv12_compositor_contract_tests -j8` | **exit 0** |
| NV12 contract suite | `CHRONON3D_RUN_CUDA_TESTS=1 ./build/chronon/linux-video-test/tests/chronon3d_nv12_compositor_contract_tests` | **8/8 cases, 329/329 assertions PASS** (3.28 s via CTest) |
| Native decoder teardown | `ctest --test-dir build/chronon/linux-video-test -R '^chronon3d_native_decoder_teardown_tests$'` | **PASS** (27.45 s) |
| Native decoder | `ctest --test-dir build/chronon/linux-video-test -R '^chronon3d_native_decoder_tests$'` | **PASS** (0.21 s) |
| Render graph | `ctest --test-dir build/chronon/linux-video-test -R '^chronon3d_render_graph_tests$'` | **PASS** (88.98 s) |
| Backend registry | `python3 .tmp/p1p3_audit/run_registry_chunks.py` | **162/162 cases PASS**, 14 fresh processes, `ALL_CHUNKS_PASS` |
| Diff hygiene | `git diff --check` | **exit 0** |

The registry suite still needs fresh processes because a single process hits
the NVIDIA ICD `vkCreateInstance -9` limit after roughly 26 instances. The
chunked run is the validated workaround.

## Per-ticket result

| Ticket | What was checked | Result |
|---|---|---|
| **P2** | `kLayerBatchSlots=6`, one `CUevent` per slot, sync-before-overwrite, event recorded after kernel launch, destructor drains; 12-frame async test | **PASS**: 8/8 NV12 cases, 329/329 assertions; the repaired test queues 12 frames without intermediate sync and matches the sync reference |
| **P4** | CUDA 64-bit tile mask and Vulkan 32-bit tile mask preserve painter order; 65-layer chunk boundary; per-layer-region bit equality | **PASS for correctness**: 65-layer test 196/196 assertions; registry 162/162. The 1/4/16/32-layer benchmark was **not rerun**, so no speedup claim |
| **P5** | `deferred_surface_releases_scratch` + `std::swap`, both buffers retain capacity, `destroy_all()` clears scratch | **Implementation matches ticket**; registry and render-graph suites pass. Zero-allocation instrumentation was **not run** |
| **P6** | Per-config gate, daemon default for certified CQP shapes, fail-closed parked-session rules; 5-job NVENC reuse chain | **PASS at the test gate**: native decoder teardown passes. The 10-clip/48-frame encoder benchmark was **not rerun** |
| **P7** | Bounded `EAGAIN` retry with `drain_packets()` before retrying the same frame, release only after acceptance, `av_frame_ref` seed, `CUevent` pool | **Implementation matches ticket**; teardown suite passes. No direct `EAGAIN` fault-injection test exists, so this is code inspection + integration, not an injected-backpressure proof |
| **P8** | `read_gpu_timestamps()` uses a stack `std::array` sized by `kCompiledPassTimingCapacity`; no `std::vector` | **Implementation matches ticket**; render-graph and registry suites pass; no allocation-counter test |
| **P9** | Logical `(config_path, input_space, working_space)` lookup before any stat; `last_write_time`/`file_size` only on the first miss | **Code inspection PASS**. This build has `CHRONON3D_ENABLE_OCIO=OFF`, so the OCIO runtime and syscall trace were **not executed** |

## Limits

- No same-workload pre-patch A/B was built; the patches are already in HEAD.
- P4 benchmark and P6 multi-clip benchmark were not rerun in this pass.
- P5 zero-allocation acceptance is not instrumented; it is supported by code
  inspection and the existing suites, not by an allocation counter.
- P7 has no injected `EAGAIN` test; the retry path is verified by inspection
  and the NVENC teardown integration.
- P9 runtime behavior is not verified with OCIO OFF.
- No commit was created.

## Artifacts

- `nv12_full.log` — full NV12 contract output, including the 12-frame async and
  65-layer tests.
- `../../tickets/archive/TICKET-PERF-P2-DIRECT-YUV-PINNED-HOST-BUFFER-SYNC-20261117.md`
  and `../../tickets/archive/TICKET-PERF-P4-DIRECT-YUV-AND-LAYER-BATCH-TILE-CULLING-20261117.md`
  carry the corresponding verification notes.
