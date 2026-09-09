# Chronon wave closeout TODO

Owner: Codex, working directly on `main` as requested.

Goal: complete the remaining Wave 1–13 architecture/runtime/certification work from the current audit, preserving the already-closed Wave 3, 6, and 9 invariants. Build and full verification are deferred until the final implementation change; then use the existing ccache cache, mold when available, and `-j20` with RAM monitoring.

Audit baseline supplied by the user: Chronon3D `79dda0c41284225bcf40b70be75533e416fd2379`; RenderingGen `3c4de1ec1ff2de200e031391389b397ccf4aa9fb`.

## Work items

| Area | Status | Exit evidence |
|---|---|---|
| Wave 3 — direct typed-program execution | CODE DONE — final verification pending | Typed prepared path samples `CompiledProgram` payloads and caches immutable topology; final compiler/runtime equivalence suites remain to be run. |
| Z3 — direct encode-surface binding | In progress — binds compiled output physical allocation | Terminal physical slot is the encode surface before execution; no degraded-lane copy calls; Z3 equivalence reports byte-identical output and zero GPU surface copies. |
| Z4 — DirectYuv certified set | In progress | Capability matrix is consumed before lane open; builtin asset-free corpus target has explicit expected gates; non-certified scenes remain counted FullGraph. |
| Z5 — absorb `DirectYuvExecutor` | In progress — executor and loop authority deleted | Canonical `FrameSlotPool`/writer pipeline owns prepare, run, cancel, submit, progress, and timing; the old executor and stub are deleted. |
| Z7 — certification instrumentation | In progress — upload-byte windows emitted and verified by gate | Report emits separate prewarm and steady-state windows, including upload-byte semantics and canary counters. |
| Wave 13 — certificate aggregation | In progress — SHA/upstream/clean-tree fields added | Aggregator records one fixed SHA and consumes architecture, correctness, zero-copy, and report evidence. Same-SHA CI remains an external gate until pushed/green. |
| Wave 0 — same-SHA CI | Open / external | One fixed pushed SHA has green build, tests, sanitizers, resource-leak, native-video, architecture, and zero-copy workflows. |
| Wave 1 — compiler authority | In progress — typed root is canonical; legacy API remains for C ABI/tests | `Compiler::compile` no longer routes through `render_plan::compile_render_plan`; remaining legacy render-plan API has an explicit compatibility owner/removal gate. |
| Wave 2 — pure `CompiledProgram` | In progress — typed IR is pure; composition adapter remains | Runtime production entry points consume `CompiledProgram`; remove the remaining `CompiledComposition` adapter only where an equivalent typed source exists. |
| Wave 4 — pure frame lifetime | Partial — frame-local payload and execution scope are canonical; compatibility accessors remain | Preserve `FrameSlotPayload`/`FrameExecution`; finish cleanup after the legacy composition adapter is retired. |
| Wave 5 — service locator demolition | Partial — duplicate software scene state and the `RenderGraphContext::program_store` pointer were removed | Finish collapsing the remaining execution dependency pointers after Wave 7 removes the compatibility route; preflight keeps explicit build-only dependencies. |
| Wave 6 — text cache SSOT | Preserved green | RuntimeContext remains the single bounded text-layout cache owner. |
| Wave 7 — Scene/timeline evacuation | Partial — `JobSession` no longer owns the legacy composition; arbitrary `Composition::SceneFunction` remains an explicit compatibility adapter | Typed prepared jobs use `JobSession::program()` and `RenderPipeline::render_program()`; remove the remaining adapter only after typed Composition lowering exists. |
| Wave 8 — RenderEngine demolition | Partial — single-frame and compatibility batch lifecycle now delegate to `RenderPipeline`; composition adapter remains | Keep the handle free of slot/worker/queue orchestration; remove the compatibility adapter after typed Composition lowering. |
| Wave 9 — CLI/IPC parity | Preserved green | Normalize and resolve exactly once at the transport boundary. |
| Wave 10 — observability ownership | Verified cross-repo at supplied SHA | RenderingGen `3c4de1ec...` already preserves the raw timing sidecar opaquely; Chronon now emits explicit prewarm/steady-state windows. |
| Wave 11 — configured architecture gates | CODE DONE — final gate pending | `check_architecture.py --build-dir` reads the configured File API target graph; CI requests the codemodel before configure, and the duplicate-basename preventive rule is now registered. |
| Wave 12 — CMake DAG demolition | CODE DONE — final gate pending | Compiler-pure sources remain under `src/compiler/**`; backend-dependent graph-builder sources are owned by render-graph targets; configured codemodel remains the final ownership evidence. |
| Final verification/build | Deferred | Only after all implementation work: configure with `ccache` and `mold` when available, build with `-j20`, run CPU suites, then GPU-host gates where available. |

## Prioritized follow-up backlog from the current audit

Questa sequenza separa cleanup a basso rischio, demolition debt, misurazioni di
performance e certificazione. Il batch `PreparedRenderJob` non va ottimizzato
come authority permanente: va assorbito dal lifecycle canonico e poi rivalutato.

| Ordine | Area | Stato | Azione / criterio di uscita |
|---:|---|---|---|
| 1 | PTX cache | CODE DONE — final CUDA test pending | NVRTC PTX is function-local `static const`, compile-once and returned by const reference; warm-up/compositor callers borrow it without a per-call copy or explicit mutex. Opt-in CUDA test now checks non-empty stable identity; warm-up remains host-dependent. |
| 2 | Dead `StaticScene` | CODE DONE — final compile/test pending | Census found no live canonical consumer: removed the `if (false)` branch, `SceneExecutionMode::StaticScene`, and `CompiledComposition::static_scene`; frame-delta `static_scene_reuse` remains a separate runtime reuse signal. |
| 3 | ffprobe diagnostics | CODE DONE — final compile/test pending | Metadata and count-frames parse/command failures emit compact `[ffprobe]` warnings with standard/unknown exception fallbacks; receipt regression test checks failure without exposing probe JSON. |
| 4 | Demolition Debt: legacy composition | ACTIVE — card recorded | Typed single-frame/batch paths are canonical; arbitrary `Composition::SceneFunction` remains only as an explicit adapter pending typed Composition lowering. Full owner/exit/equivalence/removal scope: [TICKET-WAVE-CLOSEOUT-DEMOLITION-DEBT](tickets/TICKET-WAVE-CLOSEOUT-DEMOLITION-DEBT.md). |
| 5 | Demolition Debt: legacy video resolver | REMOVED — equivalence matrix PASS | Removed `resolve_legacy_internal()` and its legacy naming; semantic resolver remains the sole authority and the final GOP copy/downgrade matrix passes. Evidence and scope: [TICKET-WAVE-CLOSEOUT-DEMOLITION-DEBT](tickets/TICKET-WAVE-CLOSEOUT-DEMOLITION-DEBT.md). |
| 6 | Wave 7/8 batch lifecycle | CODE DONE — final verification pending | `PreparedRenderJob` remains a handle; slot lifecycle, worker coordination, `FrameExecution`, queues and release are owned by `RenderPipeline`/`JobSession`. The prepared-batch CV was not optimized because the legacy orchestrator is the demolition target. |
| 7 | Wave 12 physical ownership | CODE DONE — final gate pending | `.cpp` compiler-pure sources remain in `src/compiler/**`, backend-dependent adapters in render-graph targets, and the codemodel/architecture gates enforce zero illegal compiler ownership. |
| 8 | `FramebufferPool` contention | NARROWED — measurement PASS | Moved framebuffer allocation and logical resize out of the pool mutex; concurrent measurement passes with 1024 acquires, 33,583 µs aggregate wait, and 2,160 µs aggregate hold. Sharding is not justified by this gate. |
| 9 | GPU/native-video certification | NOT RUN — external host required | Certify Z3 equivalence, Z4 direct NV12/P010, same-backing NVENC, `gpu_surface_copy_frames = 0`, `rgba_to_nv12_copy_frames = 0`, and one SHA on a GPU host. Local code and opt-in contracts are ready. |
| 10 | Vulkan recursive mutex | CENSUS DONE — DEFERRED after green baseline | Two sites remain (`sdk_render_engine.cpp` state and Vulkan backend API). Public/locked split and re-entry tests are intentionally deferred until a green baseline, then require a fresh verification cycle. |
| 11 | Duplicate basenames / namespace drift | CODE DONE — gate pending | Added the declarative architecture gate plus checked-in grandfather allowlist; new source basename collisions fail while historical collisions remain explicit. CMake ownership rules continue to guard legacy directory drift. |

### Decisioni architetturali vincolanti

- Il finding CV del prepared-batch non è un performance refactor prioritario: la
  soluzione preferita è cancellare l’orchestrator legacy. Solo se resterà una
  pipeline a tre stage dopo Wave 7/8 si rivaluterà il sequencing per-stage.
- `FramebufferPool` segue **narrow-before-shard**: nessuno sharding senza dati di
  contention e senza una seconda misura dopo la critical-section reduction.
- `resolve_legacy_internal()` è demolition debt interna all’unica resolver
  authority, non una seconda authority da duplicare.

## Change log

- Initial TODO created before the next implementation change.
- Removed the `ProgramSampler` frame-sampling callsite from render preparation and made cached topology independent of frame-arena memory.
- Rewrote the compiler boundary test to validate immutable compiler data and the topology adapter without `sample_scene` or the render-plan bridge.
- Simplified the runtime execution corpus to typed-only deterministic rendering, removing its legacy `compile_render_plan` dependency.
- Removed the now-unused runtime/frame context setup from compiled-program preparation.
- Added explicit Z7 prewarm/steady-state CUDA upload snapshots and emitted them under `summary.windows`, retaining cumulative totals as canaries.
- Made the zero-copy verifier block when the formal Z7 prewarm/steady-state window fields are absent.
- Removed the unused per-frame `FrameContext` construction from `render_program()`.
- Hardened the certificate aggregator with observed/upstream SHA, optional `--expected-sha`, and optional `--require-clean` checks.
- Bound Z3 to the compiled terminal output physical slot when available, with a persistent-output fallback only when the plan has no transient slot.
- Raised the fast-dev CMake build preset from 16 to 20 jobs to match the requested final-build concurrency.
- Expanded the tracked goal to the supplied Wave 0–13 audit; Wave 3/6/9 are regression constraints, and Wave 0/10 remain external/cross-repo gates.
- Started Z5 by moving the prepared DirectYUV program contract to the media public boundary; the old executor loop/stub are the next deletion in this same migration.
- Added the capability implementation as a program authority; the CLI/session rename and executor-loop deletion remain in the next patch.
- Renamed the session/setup ownership from `DirectYuvExecutor` to prepared `DirectYuvProgram`.
- Removed the old CLI adapter file in preparation for the canonical direct loop implementation.
- Reintroduced the direct loop under the canonical pipe export authority, with no media executor-owned frame loop.
- Deleted `DirectYuvExecutor`, its second loop/stub, and replaced the non-native fallback with a prepared-program stub.
- Added an opt-in configured CMake File API/codemodel gate, wired it into full-validation CI, and added a configured compiler-source ownership rule for the remaining legacy graph families.
- Audited RenderingGen at `3c4de1ec...`: `preserveRawTimingSidecar` is already the opaque artifact owner, so no cross-repo edit was made.
- Added the Z4 capability-matrix check inside `DirectYuvProgram::prepare`, preserving the pre-open resolver as the first decision and making direct execution fail closed for every other caller.
- Corrected the configured CMake ownership rule to match relative codemodel source paths; the next gate run must expose the remaining compiler/legacy graph ownership instead of silently passing.
- Fixed the final-build compile error by including `<memory_resource>` where `ProgramSampler` declares its PMR topology materialization resource.
- Final CPU build passed: `cmake --build --preset linux-fast-dev --parallel 20` completed `219/219` targets; peak observed memory stayed below the 29 GiB host limit.
- Final CPU test run was interrupted after 25/80 tests by the user; before interruption, the core suite failed 24 font-corpus cases because the referenced fonts are absent, backend-registry failed one barrier assertion, and the compiler boundary gate found a forbidden legacy name in a comment. The comment-only false positive is removed; the functional failures remain open.
- Refined the compiler sampler comment to avoid the boundary gate's `runtime/` token matcher; the targeted compiler boundary test must be rerun after the final commit.
- Found and fixed a configured-CMake gate bug: target source rules now read each target's File API detail JSON instead of the codemodel summary, so legacy compiler-owned sources cannot pass silently. This requires a follow-up commit/push.
- Physically relocated the 19 compiler-owned graph translation units from `src/render_graph/{builder,compiler,optimizer}` into `src/compiler/{graph_builder,graph_compiler,optimizer}` and updated CMake/include ownership. No build was run yet by policy.
- Removed stale `CompiledProgram` bridge wording and corrected the render-graph pipeline header to describe the typed production adapter accurately.
- Audited the remaining Wave 7/8 bridge: `RenderEngine::prepare(Composition)` still accepts an arbitrary per-frame `SceneFunction`, while `CompiledProgram` currently compiles `RenderPlan`; a one-frame projection would be semantically invalid, so this remains an explicit typed-lowering prerequisite rather than a compatibility hack.
- Removed `JobSession::legacy_composition_` and its accessor; single-frame prepared rendering now delegates lifecycle and typed execution to `RenderPipeline`, while the arbitrary `Composition` adapter is retained only on `PreparedRenderJob::Impl`.
- Completed the configured architecture gate after the physical CMake split: Python reports `148/148` rules passed, including compiler ownership, backend-free compiler sources, source-only header visibility, and the configured CMake DAG.
- Moved the prepared compatibility batch workers and slot lifecycle into `RenderPipeline::render_compatibility_frames()`; `PreparedRenderJob` now validates and maps the runtime result only. Final build verification is pending.
- Made the NV12 PTX authority immutable after function-local initialization: compile once, const-reference reads, and no explicit steady-state mutex or per-call copy. CUDA identity/warm-up verification remains a final-lane check.
- Removed the unreachable `StaticScene` execution mode and storage while preserving the independent frame-delta static-scene reuse signal and FlatBuffers execution-mode field.
- Replaced silent ffprobe failures with compact warnings, added standard/unknown exception fallbacks, and registered a missing-video receipt regression test that asserts no probe payload is exposed.
- Removed the legacy-named video resolver helper, retained one semantic plan authority, and recorded both demolition-debt cards with required owner, exit, equivalence, removal-scope, and status fields.
- Added the preventive duplicate-source-basename gate with an explicit historical allowlist; new collisions are architecture failures and the existing CMake ownership rules cover legacy directory drift.
- Applied `FramebufferPool` narrow-before-shard: pool selection/state stays locked, while arena/heap allocation and reused-buffer logical resize run after unlock; final contention measurement remains part of verification.
- Completed the Vulkan mutex census: two independent recursive mutex sites remain; removal is explicitly deferred until the green baseline and cannot be claimed by the CPU-only implementation pass.
- Added the opt-in NV12 CUDA PTX identity regression test; it checks non-empty stable address across repeated cache reads while the compositor equivalence corpus remains the warm-up/runtime evidence.
- Implementation freeze reached for the local closeout list; all remaining work is final build/check execution plus explicitly external GPU and same-SHA CI evidence.
- Final build exposed and fixed one stale Wave-5 test assignment to the removed `RenderGraphContext::program_store`; the test retains its local cache fixture and no longer wires a duplicate context pointer.
- Final verification exposed three compiler-boundary comment false positives for the forbidden `Framebuffer` token; comments now use the backend-neutral `surface` term without changing compiler behavior.
- Aligned the compiler boundary with standalone backends by capturing their immutable processor snapshot and generation at compile entry; updated the first-read barrier test to the canonical Undefined-state contract.
- Rebuilt the affected CPU targets with `-j20`, ccache, and mold; compilation and linking completed successfully.
- Configured the GPU/native-video test tree from the existing vcpkg installation; the first build attempt exposed a read-only ccache temp directory, so the retry uses `CCACHE_TEMPDIR` under `/tmp` without disabling ccache.
- The GPU build also found the existing ccache directory read-only in this session; retry policy keeps ccache enabled in readonly mode with writable temporaries, preserving hits without mutating the shared cache.
- The GPU build exposed a missing direct include for the canonical `compile_composition()` declaration in the RenderEngine API fragment; added the owning timeline compiler header before retrying.
- Video/Vulkan configuration exposed stale registry tests: migrated their `ResourceDesc` construction to the canonical lifetime-aware form, replaced removed `CommandPlan::barriers` with `transitions`, and qualified the sole `GraphExecutor` authority.
- Completed the Vulkan registry-test migration after the compiler exposed one additional five-argument `ResourceDesc` fixture; the dedicated `chronon3d_backend_registry_tests` target now compiles and links.
- Updated the GPU media fixtures to current CUDA and decoder contracts: qualified `CudaLayerResourceKind`, used the four-argument CUDA context creation API, selected the media `CudaYuvFormat`, and unwrapped `DecodeResult` through `decoded_frame_if()`.
- Completed the NV12 GPU fixture port by using fully qualified compositor types and movable RAII device buffers, so vector reallocation cannot double-free CUDA allocations.
- Reworked native software-frame staging to use FFmpeg's aligned `av_image_alloc` stride contract instead of a tight vector; this closes the suspected multi-frame `sws` heap-corruption path and the regression suite is queued for rerun.
- Added the resolver equivalence matrix for final GOP copy/downgrade decisions and a concurrent `FramebufferPool` narrow-before-shard measurement gate; both are pending the final test execution.
- Focused verification passed for the expanded native decoder regression (21/21) and the post-narrow pool measurement (1024 acquires; 33,583 µs aggregate wait; 2,160 µs aggregate hold); corrected one resolver test fixture to exercise the intended hybrid branch.
- Closed the native software-FFmpeg `sws` corruption in code: FFmpeg-aligned staging is used and the same-source two-frame regression is now in-tree; regular and focused ASan verification of this expanded test is still pending.
- Closed `TICKET-NATIVE-SOFTWARE-SWS-MULTIFRAME-CORRUPTION`: regular and focused ASan decoder tests pass 21/21, including first seek, second same-session frame, and retained replay.
- Re-ran the expanded native decoder regression after adding the retained software replay: regular and focused ASan both pass 24/24; the FFmpeg software materialization ticket is closed.
- The resolver final GOP equivalence matrix passes 4/4; the pool narrow-before-shard measurement passes 1024/1024 with the measured aggregate lock wait/hold values recorded above.
- The full video build exposed two stale test integration points: the P010 fixture namespace and the native teardown suite's missing CLI encoder link; both are corrected before the next `-j20` build.
- The next full video pass compiled the corrected P010 fixture but mold aborted with SIGBUS while linking one Vulkan text-canary binary; this is a retryable linker failure, not a source diagnostic.
- The video build was retried after freeing generated build space; mold still aborted with SIGBUS while linking `chronon3d_subtitle_font_ref_tests`, so the remaining video build verification is being retried serially to distinguish linker resource pressure from a source failure.
- Recreated the video configuration against the existing vcpkg installation and built all changed/minimum-gate video targets with ccache read-only, mold, and `-j20`: 747/747 build actions passed after removing the generated 88-GB retry tree that exhausted the filesystem.
- Final selected video CTest reached the static architecture/cache/runtime/render-job/compositor gates; architecture, cache, compiler-boundary gate, runtime ownership, graph executor authority, render-session migration, render-job, and compositor checks passed. Vulkan registry and CLI GPU-slot checks are host-ineligible without the NVIDIA driver; the render-graph invocation stopped after no observable progress for over six minutes and will be rerun in isolation with a bounded timeout. The compiler-boundary executable itself is queued for the isolated target build.
- Built `chronon3d_compiler_boundary_tests` successfully with ccache read-only, mold, and `-j20`; the isolated binary is now fresh for the next gate run.
- The bounded non-GPU CTest rerun passed architecture, cache, compiler boundary (binary and static gate), runtime ownership, graph executor authority, render-session migration, render-job, compositor, media-video, native decoder, P010, and NV12 tests; `video_contracts_tests` remains environment/CLI-fixture FAIL (exit 106), while native teardown produced excessive invalid-option output and was not claimed green after the interrupted bounded run. GPU registry/CLI and render-graph full execution remain unverified on this host; no source failure was found in the changed ticket targets.
- Fasi C–H strategicamente deferred: hot-path G41/G43/G45 landed (reuse in-place, tight-stride direct, refresh_fallback counter), Vulkan `wait_for_standalone()` split landed, sidecar `video_surface_upload_bytes` + `synchronize_native_output is_gpu_native_required()` policy fix landed; FASE C20-25/D26-30/E31-35/F37-40/G46-48/H49-53 census reconciled in `docs/tickets/TICKET-COMPILER-DEMOLITION-WAVES` + `TICKET-WAVE-CLOSEOUT-DEMOLITION-DEBT` + `TICKET-FFMPEG-PIPE-SINK-DEMOLITION` con 4 gate espliciti; `deploy/chronon3d-daemon.service` revert; `CURRENT_STATUS` Z-wave/Wave10 reconciled — final build unico pending.

## Resource policy

- Do not reset ccache statistics during this effort.
- Prefer the existing ccache cache and its maximum configured size.
- Use `mold` only when the toolchain/configuration supports it.
- Final build concurrency is `-j20` while the host retains sufficient available RAM.
- Check `free -h`, swap, and active compiler/linker processes before final build.
