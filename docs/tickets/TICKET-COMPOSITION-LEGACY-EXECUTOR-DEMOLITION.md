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
- `CompiledArtifactCache`, whose serialized artifact and identity are based on
  `CompiledComposition`.

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
