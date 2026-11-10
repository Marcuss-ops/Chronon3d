# TICKET — Composition legacy executor demolition

**Status:** ACTIVE — inventory complete, replacement contract and equivalence
gates remain open  
**Priority:** P1  
**Owner:** compiler/runtime maintainers

## Objective

Make `CompiledProgram` the only production execution input for Chronon3D's
RenderPlan and export paths. Move arbitrary `Composition` authoring and its
`CompiledComposition` executor out of the renderer core, then remove the
compatibility executor once its consumers have migrated or have been retired.

This ticket covers the remaining non-RenderPlan entrypoints. The RenderPlan
video route is already typed: FullGraph, DirectYUV and GOP receive
`CompiledProgram`; `make_video_compatibility_view` and its translation unit
have been deleted. That migration is tracked in
[`TICKET-COMPILER-DEMOLITION-WAVES.md`](TICKET-COMPILER-DEMOLITION-WAVES.md).

## Why the legacy path remains

`Composition` can contain authoring callbacks that are not represented in
RenderPlan or `CompiledProgram`. There is no general `Composition` to
`CompiledProgram` lowering, so deleting the adapter today would either break
those callers or silently change their render semantics. A conversion that
cannot preserve a callback must fail explicitly; it must never omit it or
replace it with a guessed static value.

## Production consumer inventory

The current compatibility surface includes:

- `RenderEngine::prepare(const Composition&)`, which calls
  `compile_composition()` and stores a `CompiledComposition` on
  `PreparedRenderJob::Impl`.
- `PreparedRenderJob::render()` and `render_frames()`, which dispatch through
  `RenderPipeline::render_compatibility()` and
  `render_compatibility_frames()` when the job has no typed program.
- `SoftwareRenderer::render_compiled()` and its prepared-composition memo.
- `render_compiled_composition_frame*()` and temporal execution in the render
  graph.
- `runtime::prepare_render()` and renderer warmup overloads for
  `CompiledComposition`.
- CLI registry jobs, which construct and compile a `Composition` when a
  request supplies no `CompiledProgram`.
- SDK `RenderFileRequest`, which accepts `Composition*` and
  `CompiledComposition*` alongside `CompiledProgram*`.
- DirectYUV legacy overloads used by non-RenderPlan jobs.
- The former `CompiledArtifactCache` `CompiledComposition` FlatBuffer
  serializer, retired in the 2026-11-10 cache migration below after the source
  audit found no consumer. The active CUDA NV12 artifact path uses the typed
  `ProgramArtifactKey` binary-artifact methods.

These are not dead declarations. They need migration, isolation as an optional
legacy module, or explicit retirement before deletion.

## Work plan

1. **Freeze the input contract.** Decide which legacy `Composition` features
   have a typed RenderPlan equivalent and document callback features that
   cannot be lowered. Map each SDK, CLI and cache consumer to its migration or
   retirement path.
2. **Migrate typed callers.** Make RenderPlan, daemon/IPC and video export use
   only `CompiledProgram`, including still/sequence export, preparation,
   warmup, cancellation and cache access. Keep settings and per-frame state
   frame-local.
3. **Isolate the authoring API.** Move remaining `Composition` compile/render
   entrypoints behind a separately linked compatibility target if external
   callers still require them. The minimal renderer target must not link that
   target.
4. **Migrate or retire public consumers.** Replace SDK and registry-based CLI
   composition inputs, or publish an explicit breaking-version retirement
   decision for callbacks that have no typed equivalent.
5. **Delete the old executor and artifacts.** Remove composition evaluation,
   render adapters, old DirectYUV overloads, cache serialization and CMake
   sources only after no supported consumer depends on them. Bump cache format
   and identity where the serialized authority changes.

## Exit conditions

- No production RenderPlan, CLI, daemon, SDK or export request requires
  `CompiledComposition`.
- The minimal renderer target has no dependency on the legacy authoring or
  composition executor target.
- Every retained authoring callback has either an explicit typed representation
  with matching semantics or a documented unsupported-input error.
- Cache readers reject the old artifact format safely; new cache identity is
  covered by round-trip and invalidation checks.
- Compatibility render symbols and `CompiledComposition` serialization have
  no supported callers and are deleted, or are confined to the explicitly
  optional compatibility target.

## Equivalence gates

- Same-SHA `linux-fast-dev` and `linux-video-fast-dev` builds (`-j12`).
- Still, sequence and video decoded-pixel comparisons on the existing golden
  corpus, including camera samples, shutter subframes, motion blur, alpha,
  nested precomps and layer hierarchy.
- Cancellation and failure-path checks during sampling, preparation, render
  and encode; deterministic output under repeated and reverse-order rendering.
- DirectYUV and GOP parity, including copy-only and mixed copy/re-encode plans.
- Cache serialization, format-version rejection and content-identity checks.
- Architecture gate prohibiting new production callers of the compatibility
  executor after migration begins.

Current build success does not satisfy these equivalence gates. The last
RenderPlan video and header-cleanup tranche built `chronon3d_cli` with `-j12`,
but runtime and pixel-equivalence tests were not run.

## Demolition debt

- **Owner:** compiler/runtime maintainers.
- **Reason:** arbitrary `Composition` callbacks and their consumers have no
  complete typed lowering yet.
- **Exit condition:** the exit conditions above are met on one reviewed SHA.
- **Equivalence test/gate:** still/sequence/video pixel parity, camera and
  shutter parity, cancellation/determinism, cache compatibility and same-SHA
  builds.
- **Removal scope:** `CompiledComposition` compile/evaluate APIs;
  `render_compatibility*` and `render_compiled_composition_frame*`; software
  renderer memo; DirectYUV legacy overloads; SDK and CLI legacy request fields;
  cache serialization/identity; associated CMake targets and anti-resurrection
  rules.
- **Status:** ACTIVE. The typed RenderPlan video bridge is removed; remaining
  authoring and compatibility APIs are still live and must not be deleted
  without migration or explicit retirement.

## Anti-resurrection gate — 2026-11-05

The architecture rules now pin production references under `include/`, `src/`
and `apps/` so the compatibility surface cannot grow while consumers migrate:

- `compile_composition(`: 15 current references.
- `render_compatibility(` / `render_compatibility_frames(`: 6 current references.
- `render_compiled_composition_frame*(`: 9 current references.

The caps allow these counts to decrease as callers are removed and fail if a
new reference raises them. The full architecture gate passes **217/217 rules**.
This prevents new debt; it does not satisfy the migration or parity exit gates.

The full architecture checker was rerun on the current checkout on 2026-11-05
after the cache identity changes and passes **217/217**. The pinned production
caller counts remain 15 / 6 / 9. No executor consumers or arbitrary callback
contract was migrated in this tranche, and pixel-equivalence gates remain
unrun; the ticket stays ACTIVE.

## Checkout audit — 2026-11-09

The current CLI boundary still has two execution inputs. `RenderRequest` can
carry either `CompiledProgram` or `CompiledComposition`; otherwise
`resolve_render_request()` calls the registry's `construct() -> Composition`
and then `compile_composition()`. The registry has no typed-program factory,
and the public callback cannot be lowered generically without losing behavior.
The CLI loop and SDK still dispatch the compatibility view for those callers.

A source search under `include/`, `src/` and `apps/` found 17 references to
`compile_composition(`, 3 to `render_compatibility(`, 4 to
`render_compatibility_frames(` and 11 to `render_compiled_composition_frame`.
These are raw search counts, not a substitute for the architecture gate's
production caller census. No compatibility consumer was migrated in this
audit. Pixel equivalence, cache-version rejection, cancellation and
determinism gates remain **NOT RUN**; status remains **ACTIVE**.

The requested default build completed on this checkout with
`cmake --preset linux-video-release` followed by
`cmake --build build/chronon/linux-video-release -j14` (**588/588**, exit 0).
That is build evidence only and does not satisfy the migration exit conditions.

## Typed cache migration — 2026-11-10

`CompiledArtifactCache` now exposes only the typed program-artifact lane used by
the CUDA NV12 compositor (`load_program_artifact` /
`save_program_artifact`), keyed by target, compiler, flags, semantic fingerprint
and ABI identity. The old `CompiledComposition` save/load API, `LoadedArtifact`,
FlatBuffers schema/generator, `.cart` reader and name-based contains/remove
methods were deleted after a full source search found no consumers. The
`clear()` operation also removes stale `.cart` files without deserializing or
executing them. The focused cache test covers typed identity and payload
round-trip. The updated `chronon3d_cache_tests` target was compiled from the
current sources and passes **206/206 cases, 103,977/103,977 assertions**; its
new regression confirms `clear()` reclaims both `.pbin` and stale `.cart`
files. The full `-j14` release build passed after the removal, and the updated
architecture gate passes **218/218**, including the new
`compiled_artifact_cache_is_typed_only` anti-resurrection rule. This deliberately
retires the old public C++ cache methods; in-repository clients now use the
typed key API. It removes the last active internal cache adapter that could
hydrate the compatibility executor; it does not migrate the separate SDK,
registry, RenderEngine, renderer, DirectYUV or daemon consumers.

## Typed-particle migration audit — 2026-11-09 (superseded)

The current working tree contains a RenderPlan video-preparation fallback for
particle plans. It calls `render_plan::compile_render_plan()` and sends the
result through `CompiledComposition`; this preserves particle output but
violates the one-canonical-`CompiledProgram` video invariant. The repository
architecture gate now reports **216/217**, with its sole failure at
`render_plan_video_uses_one_canonical_compilation` in
`apps/chronon3d_cli/commands/render/render_plan_preparation.cpp`.

The underlying compiler gap is concrete: `compiler::NodeKind` and
`StaticLayerProgram` have no particle kind/payload, `stages::lower_scene()` does
not lower `LayerType::Particles`, `TypedProgramSampler` does not sample emitter
instances at frame time, and `scene_materializer.cpp` has no typed particle
materialization. The legacy RenderPlan materializer samples the emitter at
`FrameContext::effective_frame()`, creates CPU circle/image nodes and attaches
GPU particle instances. A safe migration therefore needs an immutable emitter
payload in the compiled program, deterministic frame-local particle samples,
and graph refresh/execution that preserves CPU batching, sprite assets, blend
mode, bokeh/environment depth, camera transforms and cache invalidation.

At the time of this audit, this fallback was an explicit blocker. The update
below records the follow-up implementation and replaces the stale gap list.

## Typed-particle migration update — 2026-11-09

`NodeKind::Particles`, an immutable emitter payload and deterministic
frame-local particle samples are now carried by `CompiledProgram`. The typed
scene materializer creates CPU particle nodes and GPU instance batches; graph
refresh updates animated samples. The CLI particle-specific
`compile_render_plan() -> CompiledComposition` fallback and its context field
have been removed, so RenderPlan video preparation supplies the typed program.

Evidence on the C2 1,000-particle plan:

- Release build: `cmake --build build/chronon/linux-video-release -j14` —
  exit 0, no work pending.
- Architecture gate: `python3.11 tools/check_architecture.py` — **217/217**.
  Its first pass exposed the new refresh helper's use of the old `FillType`
  alias; the helper now uses `graphics::GradientType` and the rerun is clean.
- Software decoded raw: old legacy baseline and typed output are byte-identical
  (both SHA-256
  `87e9ac05c05a3152c0634f810e51cee5f8822d4b8c724c680fc99dedfcf01607`).
- Durable raw artifacts, exact plan hash and strict-native telemetry are
  recorded in [`typed-particle-parity-20261109`](../evidence/typed-particle-parity-20261109/REPORT.md).
- Vulkan `require_gpu_native` completed and wrote a 1-frame RGBA raw artifact
  (SHA-256 `ab409c9ecef064c1e2fc1377c9f030ef40f4ac9dc0b59674496a1b7ddcb7ed93`).
  This is route/execution evidence, not a CPU/Vulkan pixel-parity claim.

The particle-specific fallback gap is closed. The parent Composition executor
demolition remains **ACTIVE**: SDK/registry/cache consumers, arbitrary callback
contract, multi-frame and mixed particle parity, cancellation/reverse-order
determinism, and DirectYUV/GOP equivalence are still open. Do not close this
ticket on the strength of the particle tranche alone.

## Current checkout verification — 2026-11-10

`cmake --build build/chronon/linux-video-release -j14` completed successfully
with `ninja: no work to do`. `python3.11 tools/check_architecture.py` passes
**217/217**; in particular, the pinned production caller counts remain
`compile_composition` 15, compatibility render 6 and legacy frame executor 9.
This confirms that the typed-particle route remains canonical and that the
anti-resurrection caps hold. It does not reduce the legacy executor inventory
or satisfy any still/sequence/video equivalence gate. The executor migration
ticket remains **ACTIVE**.
