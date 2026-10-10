# Freebuff duplication findings — verified closeout

Audit source: `freebuff-chat-2026-10-10T16-40-58.230Z.md`.

## Stato

**CLOSED — refactor delle duplicazioni completato.** Primitive hash, coverage
scanline, easing contract e parsing dei vettori hanno ora un'unica
implementazione condivisa dove i contratti coincidono; il codice sostituito è
stato rimosso. Le firme pubbliche ripetute del render graph sono un follow-up
API separato. Il caso rosso della cache resta documentato sotto come failure
preesistente e non è incluso tra le modifiche chiuse da questo ticket.

## Implemented

- `core/hash/hash_primitives.hpp` now owns `hash_combine`, the SplitMix64 finalizer,
  and its Weyl sequence step. Curve, mask, path, dark-grid, prepared-asset,
  render-plan stagger, camera shake, program fingerprint, trace ID, text scramble,
  and noise code delegate to these primitives where their input transformations
  match. Existing curve and SplitMix64 vectors protect the results.
- Card triangle scanline coverage shared by projected flat cards and material
  cards now lives in `triangle_scanline.hpp`. The callers retain their distinct
  source-over and premultiplied blend formulas. Depth interpolation remains in
  its separate path because it carries and tests per-pixel Z.
- RenderPlan easing names are declared once in `easing_contract.hpp`; animation,
  per-key, axis, stagger, and text-counter validation call the appropriate
  contract subset.
- Cache stats already use `make_generic_cache_stats`; no second stats helper was
  introduced.

## Findings kept distinct

- `fill_triangle_depth` is not a duplicate of the flat card scanline helper: it
  interpolates Z and mutates a depth buffer. The general scanline rasterizer also
  uses pixel-center coverage and compositor blending, which differs from the
  legacy card rasterization convention.
- JSON vector readers now share fixed-size numeric array decoding in
  `json_vector.hpp`. Optional-field handling and mask-specific finite/range
  checks stay at their domain boundaries.
- The two JSON Schema error handlers collect different result types and formats.
  Their common base callback is small; joining the policies would add machinery
  without removing meaningful logic.
- The report's JIT path and `RenderFrameInput` type do not exist in this tree.
  The render graph does have repeated camera/frame parameter lists across its
  public render and diagnostic entry points; those call one internal context
  factory. Replacing those public signatures with an aggregate would be an API
  migration, so it is not folded into this deletion/refactor ticket.

## Verification

- `chronon3d_core_tests`: SplitMix64 finalizer and sequence vectors passed.
  The prior full run had one existing failure at
  `test_resource_preparation_mesh.cpp:280` (expected the “static MeshLoader
  contract” diagnostic); 1,485/1,486 cases passed. Recheck this assertion on the
  next fresh build and keep it distinct from the hash changes unless reproduced
  as a regression.
- `chronon3d_effects_tests`: curve hash golden test passed; full suite passed.
- `chronon3d_renderer_core_tests`: full suite passed, including the Field2D mask
  cache-key regression.
- `chronon3d_camera_tests`: full suite passed, including card raster tests.
- `chronon3d_c_abi_tests`: full suite passed, including RenderPlan decoding.
- Targeted RenderPlan vector decoders passed for particle emitters, camera tracks,
  Field2D masks, and gradient stops (32 assertions total).
- The full C ABI suite passed before the final shared vector-reader change; after
  that change, the four targeted decoder cases passed (32 assertions). Re-run
  the complete affected suites on one fresh build before same-SHA certification.
- `chronon3d_cache_tests`: 149 cases passed when excluding the pre-existing
  dimensional-uniqueness case; its three font/tracking/placement framebuffer
  hash assertions fail even though cache misses increase. The helper replacement
  preserves the existing boost combine formula.
- The fast build for CLI, cache, core, renderer, effects, camera and C ABI targets
  completed with `-j12`.

## Prossimi TODO

1. **P1 — triage del caso cache rosso.** Riprodurre i tre assert di
   dimensional uniqueness con un binario aggiornato; verificare separatamente
   font, tracking e placement; correggere la causa o aggiornare il golden solo
   se il nuovo output è intenzionale e dimostrato. Chiusura: suite cache intera
   verde, senza esclusioni.
2. **P1 — verifica post-modifica.** Ricostruire e lanciare per intero le suite
   core, cache, renderer, effects, camera e C ABI sullo stesso build tree; il
   build `-j12` già eseguito resta evidenza del checkpoint precedente, non della
   ricompilazione successiva al parser vettoriale.
3. **P2 — API render graph.** Valutare in un ticket distinto se consolidare le
   liste pubbliche camera/frame in un input aggregato. Prima di cambiare API,
   censire consumer esterni, compatibilità e impatto ABI; nessuna rimozione è
   autorizzata dal closeout corrente.
