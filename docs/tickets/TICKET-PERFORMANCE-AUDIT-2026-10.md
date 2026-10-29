# Performance audit — October 2026

Status: PARTIAL — local audit/build gates complete; remaining native GPU/CI measurements are external certification.

## TODO — cosa manca (agg. 2026-10-28)

- [ ] Certificazione carrier/render native su host GPU e clean-SHA CI (esclusi dal perimetro locale richiesto).

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
- [x] Reduce binding scratch allocations without changing carrier lifetime,
      collisions, placement or output; record representative before/after data.
      **Before/after 2026-10-23:** the planned-alias rebuild at every
      `begin_plan_batch` used node-based `unordered_set` groups rebuilt from
      scratch — measured **12 global allocations per rebuild** (1200 over 100
      measured rebuilds, `chronon3d_vulkan_surface_materializer_tests`
      allocation guard). Groups are now retained-capacity vectors emptied in
      place (map nodes + per-group capacity survive the rebuild):
      **0 allocations** over the same 100 measured rebuilds. Carrier lifetime,
      collision policy, placement and output are unchanged — only the
      bookkeeping container changed. `is_registered_alias` additionally
      refuses self-pairs and invalid handles (matches the documented "no
      invalid handle and no self-pair may ever authorize anything" guard).
      Suites: surface materializer 27/27 (161 assertions), backend registry
      107/107 (1 913 753 assertions) green after the change.
- [x] Replace per-slot binding-index vectors with one contiguous scratch buffer
      and slot offsets; retain allocation order and carrier placement decisions.
- [x] Run the CPU-side Vulkan placement/materializer contract tests and record
      binding scratch allocation data; native GPU carrier/render certification
      remains external.
- [x] Evaluate dirty coalescing/effect scratch and adopt only validated changes.
      **Disposition 2026-10-23 (no change adopted — data below):**
      *Dirty coalescing:* the coalescing machinery is already present and
      tested (`ExecutionResolver::coalesce_dirty_regions`,
      `tile_execution_policy.cpp`; contract tests
      `test_dirty_rect_contract`, `test_dirty_rects_v2`,
      `test_frame_delta_compiler`). Extraction over **249 representative
      timing sidecars** (`docs/evidence/**`, ~1 500 frame entries: full
      121–150-frame timelines, snapshot adapters, transition reviews) shows
      `dirty_area_ratio = 1.000` on **every production frame**; the only
      sub-1.0 samples are 4 explicit diagnostic renders (`debug-black-only`,
      `debug-radial-diag`, `debug-radial-diag2`, `debug-radial-isolated`,
      `debug-radial-normal`) where the ratio is 0.0 by construction. On this
      corpus every dirty-region set would collapse to a full frame
      (`full_frame_threshold` promotion already handles it), so further
      coalescing work has **no measurable target** and would be unvalidated
      churn. Not adopted. Algorithmic reference (2026-10-23, exact geometry
      replica of `coalesce_dirty_regions` cross-validated against the pinned
      `test_frame_delta_compiler.cpp` case, 1920×1080/16 px tiles; ratio =
      executed region pixels / dirty-tile pixels): on synthetic partial-dirty
      masks the current per-row span is optimal for the dominant single-rect
      layer move (1 region, 1.00×) but covers intra-row gaps — 6.13× on two
      disjoint panel bands (1 region vs 136 at 0.99× for a per-run baseline),
      24.5× diagonal, 13.7× sparse, 2.0× glyph band. Alternatives are not
      strictly better (union-merge reaches 33.75× on the diagonal; per-run
      explodes to 240–378 regions where per-region dispatch cost is
      unmeasured), and the production corpus has no partial-dirty frames to
      improve. Any future coalescer change must measure per-region dispatch
      against per-pixel raster cost (bench corpus B08 `DirtyRectSmallMotion`
      is the intended vehicle) before adoption.
      *Effect scratch:* `EffectScratchResources` is the persistent per-session
      scalar-effect workspace (GOVERNANCE I-2/H-3); across the same 249
      sidecars `framebuffer_alloc_effect = 0` and
      `framebuffer_alloc_scratch = 0` (steady-state effect/scratch framebuffer
      allocations already zero; wiring regression ticket RESOLVED with typed
      `EffectScratchResources&`). `graph_refresh_scratch_node_builds = 0`.
      Those per-category counters, however, missed a real defect found by code
      inspection and adopted 2026-10-23: the software Echo composite copied
      the whole framebuffer per apply (`const Framebuffer current = fb;`, a
      deep copy via `copy_pixels_from`) although the composite reads each
      source pixel exactly once at its own (x, y) before overwriting it
      (history samples are independent snapshots). The copy was invisible to
      `framebuffer_alloc_effect` because it runs outside a
      `FramebufferAllocationScope(Effect)`; it lands in the generic
      `framebuffer_allocations` counter. Composite now runs in place, and
      `echo_frames`/`record_echo_frame` lost their per-call `std::string(key)`
      temporaries (transparent lookup + piecewise in-place map insertion).
      **Before/after on the exact instrument** — new contract case
      `EchoSteadyStateHasNoFramebufferTemporaries` (effects suite),
      `framebuffer_allocations` over 8 steady-state echo applies:
      **16 → 8 (2 → 1 per apply)**; the old code fails the case at 16, the
      remaining 1/apply is the history snapshot owned by `EffectScratchResources`
      (GOVERNANCE H-3 allowed owner). At 1080p the eliminated copy is
      ~33 MB/frame per echo layer. Pixel parity: Echo frame-hash canary +
      effects suite 286/286 (5 538 017 assertions) green. Methodology note:
      process-wide malloc totals (LD_PRELOAD counter) are not usable evidence
      on this host — effects suite swings ±4 and the Vulkan registry suite
      ±40 000 across identical runs (driver/thread noise); the canonical
      `framebuffer_allocations` counter is exact and run-stable. Also
      evaluated, NOT adopted: pooling the Echo history snapshots (the per-frame
      deep copy remains, bounded by the 512 MiB per-key budget) — a pooled
      ring can hand a consumer a surface the same frame's insert would
      overwrite, so it needs deferred-reuse slot budgeting plus
      aliasing/concurrency tests and golden runs; the native Echo path already
      uses the pooled design (2026-10-05 reconciliation).
- [x] Measure available native CLI lifecycle/handoff and verify output frames and
      zero-copy counters; do not remove fences or admission guards by inference.
      **Measured 2026-10-23 (native CLI reference runs `grid`/`black_glow_inter`,
      30 frames, 1920×1080, MP4 + timing sidecar + receipt):** output frames
      verified at both levels — ffprobe decodes exactly 30 frames per MP4 and
      the encoder accepted 30 packets (pts 0..29). **Lifecycle/handoff
      attribution** (job-lifecycle log fields): `slot_pool_close_ms` 0,
      `surface_release_ms` 0–8.8, `queue_close_ms` 0, `writer_join_ms` 0,
      `encoder_reset_ms` 19.5–78.5, `full_graph_reset_ms` 0–311.6 (CLI path
      only), `sink_ctx_reset_ms`/`device_runtime_reset_ms`/
      `device_reservation_reset_ms` 0 → `session_teardown_ms` 60.9–334.2
      (dominated by `full_graph_reset`), `sidecar_write_ms` 21.2–25.6,
      `receipt_build_verify_ms` 0.1 (fix-2 verification=fast),
      `receipt_file_write_ms` 0.9–1.1, `post_sidecar_to_receipt_ms`
      68.8–344.0 (dominated by teardown, not by the receipt). Handoff:
      `surface_handoff_path=host_upload` on the software-encode runs
      (`encode_handoff_ms` 0.013–0.025 first frame),
      `max_frame_to_encoder_end_ms` 228–234. **Zero-copy counters** in the
      reference sidecars are all 0 (`host_upload`/`cpu_pixel_readback`/
      `encoder_staging_copy`/`gpu_surface_copy_frames`); `black_glow_inter`
      shows 16.9 MB of content upload (one 16 MB full image + five 162 kB
      text-region uploads) — per-asset, not per-frame. Fence counters are
      populated and zero (`cuda_encode_event_wait_count`,
      `vulkan_cuda_export_fence_wait_count`): **no fence or admission guard was
      removed**. Fresh gate run on the current build:
      `tools/verify_zero_copy_end_to_end.sh --frames 30` → **ZERO_COPY_PASS
      19/19** (effective_backend=vulkan, encoder_backend=nvenc,
      surface_handoff_path=direct, gpu_native_surface_frames=30,
      gpu_native_encode_frames=30, software_encode_frames=0,
      video_pipe_fallback_frames=0), artifact re-verified at 30 frames by
      ffprobe. Measurement caveat preserved: reference runs overlapped a CPU
      build (zc-grid wall 14.1 s, startup 12.3 s) — lifecycle fields are
      smoke/attribution evidence, not a speedup reference.
- [x] Rebuild final Chronon3D CLI with `-j12`; run focused C++ and architecture
      gates and record the remaining external GPU/CI blocker (2026-10-28).

## Evidence location

Ignored local logs/artifacts: `.tmp/performance-audit/`. Original tracked diff
snapshot: `/tmp/chronon3d-audit-preexisting-20261004.diff`; original untracked
path census is saved alongside it. These are not release artifacts.

## Verified checkpoint

### 2026-10-28 local closeout

- `cmake --build build/chronon/linux-video-fast-dev --target chronon3d_cli -j12`:
  PASS; final link completed at 19:31 UTC. A following `ninja -n` reported
  `no work to do`.
- `cmake --build build/chronon/linux-video-fast-dev --target chronon3d_vulkan_surface_materializer_tests -j12`:
  PASS. `ctest --test-dir build/chronon/linux-video-fast-dev -R
  '^chronon3d_vulkan_surface_materializer_tests$' --output-on-failure`: 1/1
  PASS. This suite uses `FakeImage`/`FakeSurfaceOwner`, so it verifies CPU-side
  placement/alias contracts without claiming GPU execution.
- The focused case `planned alias rebuild is allocation-free in steady state`
  passed 4/4 assertions and measured **0 allocations over 100 steady-state
  rebuilds** (`0.00/rebuild`). The test binary (19:32 UTC) is newer than its
  source (13:48 UTC).
- `python3 tools/check_architecture.py`: `GATE_PASS`, 214/214 rules. The full
  effects suite also passed 1/1 after its binary was rebuilt.
- Remaining: actual GPU carrier/render evidence and same-SHA CI. No hardware
  result is inferred from the fake-owner unit test.

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

## 2026-10-23 — Findings from the overlay-golden per-item profiling (external measurement)

Measured on the PipelineGen overlay golden case (run v12, 38 items, 2 render
lanes) via `render_attempt_analytics` + the per-item timing sidecars
(`chronon3d.render-telemetry-summary.v1`, `chronon_timing_storage_key`).
Per-item mean: worker `render_ms` 2.748 ms of which daemon `job_wall` 1.567 ms
(`render_loop` 956, `prepare` 311, `process_startup` 186, `encode_close` 155)
and **receipt verification 933 ms** (probe 296 + full decode 634).

Defects/waste to fix in this repo (ordered by expected gain):

1. **Receipt verification holds the render slot.** `write_render_job_receipt`
   (ffprobe + sha256 + `ffmpeg -f null -` full decode) runs inside
   `execute_render_job`, i.e. before the device reservation in
   `daemon_service_ipc_render_job.cpp` is released. Release the reservation
   before building the receipt (or make the receipt async); the worker reply
   must still carry the receipt (`RequiresStructuredReply` for
   normal/certify). Expected: −933 ms/item from the serialized service.
2. **Duplicate verification of the same freshly-muxed file.**
   `verify_output_contract` (`pipe_export_finalize_encoder.cpp`) already probes
   and hashes the output (ffprobe ~15 ms + sha256 ~6 ms); `build_render_receipt`
   (`src/verification/render_receipt.cpp`) re-runs `ffprobe -show_streams
   -show_format` (~296 ms, process-spawn dominated) and recomputes sha256.
   Pass the already-computed `MediaContract` + digest into the receipt input.
   Expected: −~300 ms/item.
3. **`prepare_breakdown.font_preflight_ms` labels all of
   `runtime::prepare_render`** (`pipe_export_pipeline_setup.cpp` times the whole
   call): 245 ms mean, up to 944 ms per phrase item with many text runs. In the
   warm daemon (font/MTSDF/pool caches process-persistent) this per-item
   re-preparation should be reused across items. Expected: −~310 ms/item with
   the pool warmup.
4. **Fixed per-item overheads:** `encoder_open_nvenc` ~104 ms,
   `encode_close` ~155 ms, `plan_decode_validate` ~65 ms,
   `pool_warmup` ~65 ms. Evaluate session/pool reuse and redundant
   plan decode-validation.
   **Disposition 2026-10-23 (fix 5, reachable part closed):**
   `pool_warmup` is closed by fix 4(b) (top-up-only warmup; the
   `GpuSlotPool` and surface pools already live in the per-device
   `FullGraphSession`, so "pool per sessione" exists); the redundant plan
   work is closed by the fix-5 deep-copy removal in `prepare_render_plan`
   and `plan_decode_validate` is one decode + typed validation per unique
   plan (admission's `extract_plan_dimensions` parse measures ~1,5 ms and is
   not worth a second code path). `encoder_open_nvenc` + `encode_close`
   (~259 ms/item) are the per-artifact encoder lifecycle of the
   one-MP4-per-item contract: removing them means cross-item encoder
   session reuse, i.e. the "un piano = una sessione render" paradigm
   (PipelineGen ticket, idee di paradigma #1) — a contract change that needs
   pixel/audio parity plus three comparable golden runs before it can be
   adopted, not a micro-fix. No unmeasured encoder change was made.
5. **Image uploads at source resolution:** ~300 MB/item mean (8 full uploads,
   242 MB of image bytes) — downscale to canvas before GPU upload.
6. **Telemetry trap (measurement only):** `vulkan_device_ms` (~300 ms on every
   sidecar) is process-level device init reported per job; the actual attributed
   job startup is ~186 ms. Do not treat it as a per-item cost; label it
   process-level in the sidecar to prevent future mis-optimisation.

## Zone confuse e da bonificare (censimento 2026-10-23 sera)

1. **Doppio nome per la stessa metrica**: `prepare_breakdown.font_preflight_ms`
   resta come alias legacy di `prepare_render_ms`. Bonifica: rimuovere l'alias
   quando tutti i consumer esterni (summary, dashboard) migrano; finché resta,
   ogni lettura deve sapere che i due nomi sono la stessa colonna.
2. **Due convenzioni di "mancata misura" nella stessa sidecar**:
   `ReceiptTiming` usa `-1`, `prepare_breakdown` usa `null` (mai 0 finto).
   Bonifica: una sola convenzione (preferita `null` = non misurato).
3. **`extract_plan_dimensions` (admission) fa un parse JSON completo del
   piano** solo per leggere `canvas.width/height`, poi il path di esecuzione
   rilegge e riparse lo stesso file. Misurato ~1,5 ms (parse) + read: NON è
   il `plan_decode_validate` da 61–65 ms (quello è la validazione tipizzata
   singola). Non worth un fix dedicato: annotato per non confondere le due
   voci in futuro.
4. **Drift preset/CMakeCache**: `build/chronon/linux-video-release` aveva
   `CHRONON3D_BUILD_TESTS=ON` contro il preset (`OFF`) senza la feature vcpkg
   `tests` → configure rossa (`doctestConfig.cmake` introuvabile). Riallineato
   al preset. Regola da far rispettare: il preset è l'unica authority; mai
   `cmake -D` manuali sui build dir esistenti.
5. **Feature vcpkg fantasma `native-ffmpeg`** (passata da 5 preset, assente da
   `vcpkg.json`; warning a ogni configure). **TAGLIATA** in questa sessione.
6. **Naming non uniforme dei timing**: `ReceiptTiming` flat (`probe_ms`),
   `startup_breakdown`/`prepare_breakdown` annidati, campi `*_ms` in tre
   famiglie con regole diverse. Bonifica a bassa priorità: un solo lessico.
   Aggiunta 2026-10-23: esistono DUE campi `process_wall_ms` con significati
   diversi nella stessa sidecar (`job.process_wall_ms` ≈ 17 ms = startup
   processo, `exclusive_wall_timeline.process_wall_ms` ≈ 5,3 s = wall totale):
   rinominare il primo in `process_startup_ms`.
7. **Contatori per-categoria framebufefer a copertura parziale**: i bucket
   `framebuffer_alloc_effect`/`_scratch` contano solo le allocazioni eseguite
   dentro uno scope `FramebufferAllocationScope`; le allocazioni fuori scope
   finiscono nel generico `framebuffer_allocations`/`_unknown` ed erano
   invisibili al check "effect scratch = 0" basato sui soli bucket (è così che
   la copia Echo per-apply è sfuggita al censimento per-sidecar). Bonifica:
   avvolgere le allocazioni dei path effetti negli scope corretti, oppure
   validare sempre sul contatore generico. (Metodologia: i totali malloc via
   LD_PRELOAD su questo host hanno rumore ±4 su effects e ±40 000 su registry
   — usare solo i contatori canonici.)

Combined expectation: per-item service 2.748 → ~1.280 ms keeps the golden at
≈94 items/min on the certified 2-lane topology; the render loop (956 ms/item,
~11 ms/frame) is the remaining target to close the 100/min gate.

## Scope boundaries

Broad external B00–B11/text content-pack certification, remote runner dispatch,
and daemon production-worker measurements are separate acceptance gates. Their
absence cannot be represented as PASS. Persistent-job resources, fence removal,
sparse native video and job concurrency changes require measured correctness
and memory budgets, not merely successful compilation.

## Overlay throughput follow-up — 2026-10-23

PipelineGen's open ticket `TICKET-OVERLAY-THROUGHPUT-100-MIN.md` records a
golden service cost of roughly 2.4–2.75 s/item and about 1.3 s/item in fixed
startup, encoder, validation and receipt work. Its highest-impact renderer-side
candidate is to amortize plan preparation and encoder lifecycle across a
session of items. Keep each MP4 artifact as the compatibility baseline; compare
session reuse against the current per-item path before changing the contract.

A more disruptive alternative is to composite overlay layers into the final
timeline (or one artifact per scene), avoiding a separate MP4, mux, receipt and
publication for every overlay. This needs coordination with PipelineGen and
the remote final worker. Both options are proposals only. Acceptance requires
pixel/audio parity, unchanged idempotent receipt and retry behavior, per-class
throughput, and three comparable before/after golden runs. The existing
per-item receipt/prepare fixes do not by themselves certify either design or a
100/min result.

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

The Vulkan plan-binding scratch no longer stores one heap-backed index vector
per planned slot. It now groups allocation indexes in one contiguous buffer
using prefix offsets, preserving source allocation order within each slot and
leaving carrier discovery, collision checks, slot selection, and surface binding
logic unchanged. Validation so far: `linux-video-fast-dev` CLI build PASS,
architecture gate 207/207 PASS, and `git diff --check` PASS. Targeted Vulkan
placement tests and before/after allocation measurements remain pending. This
does not close the performance ticket.
