# Chronon — Operating Plan

> Current observed state lives in [`CURRENT_STATUS.md`](CURRENT_STATUS.md). This plan defines ordering, not release certification.

## Principle

Chronon is a deterministic media compiler/runtime. Stop expanding architecture while the current `main` is not buildable and same-SHA certified. Every change must preserve one authority per concern, measurable correctness, and explicit demolition conditions.

No branches are required by this plan; work lands on `main` only after the relevant gate for the change is satisfied.

---

## STAGE 1 — Make main true

**Status: implementation closeout landed in this checkpoint; same-SHA CI verification remains required.**

- P0 `main` self-contained/buildable.
- Finish frame-slot migration without restoring retired compatibility facades.
- Pipeline-cache consistency: one `VkPipelineCache` owner in `VulkanKernelStore`; lifecycle create/use/persist/destroy must agree.
- No new feature/refactor work while a clean-checkout compile defect is known.

Exit gate: `Chronon CI` green on the exact resulting `main` SHA.

## STAGE 2 — Close cleanup

**Status: structural Wave 0 closeout complete; explicit demolition/certification debt remains tracked separately.**

Census order:

1. Surface authority.
2. GraphExecutor closeout.
3. Descriptor authority.
4. FFmpeg subprocess closeout.
5. DeviceScheduler.
6. CLI typed config.
7. Telemetry.
8. Docs reconciliation.

Rules:

- `CompiledResourceTable` owns physical placement; Vulkan materializes/binds it.
- Do not recreate duplicate surface, executor, descriptor, scheduler, config or telemetry authorities.
- `FfmpegPipeSink` physical removal is controlled by `TICKET-FFMPEG-PIPE-SINK-DEMOLITION`; the compatibility path must not be deleted before its SDK/CI/zero-caller exit conditions are certified.
- Deferred compatibility deletion is not permission to create a replacement subsystem.

---

## STAGE 3 — Kill fixed CPU overhead

- Remove/scope compulsory hot-path timing in `LruCache`.
- Make `clear()` generation-safe.
- Lock oversized rejection counters and coalesced-waiter semantics.
- Precompute O(1) compiled-op lookup by node id.
- Reduce per-node timing/bookkeeping and use worker-local execution context.

Gate: benchmark hot-path changes; correctness and architecture unchanged.

## STAGE 4 — Determinism contract

Define the supported classes explicitly:

- `BitExact`.
- `DeterministicWithinPlatform`.
- `Approximate`.

For `BitExact`, fused and unfused output hashes must match. A 1-ULP tolerance is not bit-exact.

Gate: deterministic contract documented and fusion certification tests enforce it.

## STAGE 5 — Single synchronization authority

Target flow:

`Compiler -> ResourceUse -> ResourceStateTracker -> ResourceTransition[] -> Vulkan Sync2 adapter`

Sequence: census -> contract -> Sync2 adapter -> equivalence -> production switch -> legacy demolition -> Vulkan/CUDA sync -> certification.

Gate: one synchronization authority; legacy barrier/layout guessing removed from production paths.

---

## CHECKPOINT — Clean core

Do not advance until all are true on the same SHA:

- `main` builds clean.
- Tests/gates required for the touched areas are green.
- Duplicate authorities = 0 for surface/descriptor/sync/executor concerns.
- Compatibility debt is documented with explicit exit conditions.
- Fixed cache/executor CPU tax addressed and benchmarked.
- Determinism contract documented.

---

## STAGE 6 — Certification

- Clean checkout and clean release build.
- Runtime/resource/executor/Vulkan/CUDA/video/text tests.
- DirectYUV and multi-video checks.
- Production 5-clip benchmark.
- Record metrics, benchmark outputs and artifact hashes.

Video certification must observe the native path and record at minimum readback/fallback/conversion counters. Do not infer zero-copy from architecture alone.

Result: a clean baseline SHA that becomes the performance reference point.

## STAGE 7 — Safe optimization

- Persistent Vulkan pipeline-cache hardening: device/vendor/UUID keying, shader ABI fingerprint, corruption handling, atomic save, concurrent-daemon safety.
- ThinLTO experiment in release build; keep only with measured benefit and reproducibility intact.
- Golden diff tooling.
- Deterministic property/fuzz pipeline with seeded generation and minimized regression artifacts.
- CUDA Graph experiment only after the clean baseline.

No new generic cache/registry/queue/sync authority may be introduced here.

## STAGE 8 — New compiler architecture

Order:

1. `FrameDelta`.
2. Sparse changed regions/tiles.
3. Canonical `PixelProgram` IR with backend lowering.

`FrameDelta` precedes Render Ledger because incremental work needs a deterministic changed-work model first.

## STAGE 9 — Incremental product

- `RenderLedger` as a manifest/fingerprint product, not another cache.
- Incremental render API based on plan/frame/node/subgraph/artifact fingerprints.

Gate: unchanged work is provably reused without weakening correctness/determinism.

## STAGE 10 — AOT

Introduce `.chronon` only as a render executable containing compiled graph/resource/program/shader/asset/capability/determinism metadata.

It is not a project-editor file format.

## STAGE 11 — Advanced hardware

Only after earlier stages are certified:

- Multi-queue.
- Sparse residency.
- Multi-GPU.

---

## Explicit freeze until prerequisites are met

Do not start:

- ML rendering.
- New generic libraries/caches/registries/queue primitives/sync authorities.
- Multi-GPU spatial work.
- Sparse Vulkan residency.
- NVENC slice hacking.
- Large refactors without benchmark evidence.
- Render Ledger before FrameDelta.
- Advanced fusion before the BitExact contract.

## Definition of Done

A task is not done because code exists. Close it only with the relevant combination of implementation, caller census, negative census, regression test, architecture/developer gate, benchmark for hot-path work, docs reconciliation, clean status, and verification that the intended commit is actually the `main` tip.
