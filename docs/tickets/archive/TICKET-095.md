# TICKET-095 — render-pipeline-fps-defaults-audit (Policy E closed)

| Field           | Value                                                                |
|-----------------|----------------------------------------------------------------------|
| ID              | TICKET-095                                                            |
| Priorità        | P2                                                                    |
| Area            | `include/chronon3d/runtime/render_pipeline.hpp` fps-default audit     |
| Stato           | RESOLVED (audit-closed, no code change)                               |
| Origin          | code-review nit on commit `fc144fa2` (3-line comment-trim retro-fixup) |
| Sub-ticket of   | TICKET-render-pipeline-fps-defaults (parent audit ticket)             |
| Upstream context| commit `6df9b429` (P1 #10 — removed hardcoded `30.0f` fps defaults)    |

## Background

Code-reviewer-minimax-m3 flagged a potential audit gap after `fc144fa2` (the
3-line comment-trim retro-fixup to `75035f2b`'s default-arg chain fix).  The
audit scope referenced `render_pipeline.hpp` lines 71-79 (`render_scene`
overloads) and `src/runtime/render_pipeline.cpp:32,54` (the `.cpp`
`render_scene` member-fn bodies) for `float fps` parameter consistency.

Upstream context used for the verdict:

* `6df9b429` "fix(render): P1 #10 - remove hardcoded 30.0f fps defaults from
  core pipeline" deleted the prior literal `30.0` defaults.
* `75035f2b` "fix(render): default-arg chain fix" added `= 0.0f` sentinel to
  `debug_graph`'s `frame_time` parameter (NOT `fps`) strictly to satisfy
  C++ default-argument contiguity around the upstream `Frame frame = 0`.

## Audit verdict — Policy E (widened scope)

**No code change required.**  All scanned methods across the audited
scope already uniformly require the caller to pass `float fps` explicitly
(no default):

| Method (file)                                                          | `float fps` default |
|-------------------------------------------------------------------------|---------------------|
| `RenderPipeline::render_scene(Scene, Camera, ...)` — `include/chronon3d/runtime/render_pipeline.hpp` | (none — caller must pass) |
| `RenderPipeline::render_scene(Scene, optional<Camera2_5D>, ...)` — same header | (none — caller must pass) |
| `RenderPipeline::debug_graph(Scene, Camera, ..., Frame frame = 0, f32 frame_time = 0.0f)` — same header | (none — caller must pass on `fps`) |
| `chronon3d::graph::render_scene_via_graph(...)` — `include/chronon3d/render_graph/pipeline/render_pipeline.hpp` (free fun) | (none — caller must pass) |
| `chronon3d::graph::debug_scene_graph(...)` — same header (free fun)          | (none — caller must pass) |
| `SoftwareRenderer::render_scene(...)` — `include/chronon3d/backends/software/software_renderer.hpp` (+ `.cpp`) | (none — caller must pass) |

The Policy E outcome preserves the spirit of upstream `6df9b429` (no
hardcoded fps literal in core pipeline).  The `= 0.0f` sentinel on
`debug_graph`'s `frame_time` is **unrelated to `fps`** and exists purely
to satisfy the C++ default-argument contiguity rule around `Frame frame = 0`.

The `src/runtime/render_pipeline.cpp` lines referenced in the original
audit (around 32, 54) are `RenderPipeline::render_scene` member-fn bodies
(matching header signatures).  The widening audit additionally
cross-validated the lower-level free funs in `render_graph/pipeline/`
AND `SoftwareRenderer::render_scene` definitions; all are uniformly
no-default on `fps`.

## Acceptance criteria

- All `float fps` parameters in `RenderPipeline` lack defaults (header
  verification).
- `chronon3d::graph::render_scene_via_graph` and `chronon3d::graph::debug_scene_graph`
  free functions in `include/chronon3d/render_graph/pipeline/render_pipeline.hpp`
  continue to require `fps` explicitly (free-function side-signature check).
- No new commits needed for this audit-trail beyond the ticket stub + CHANGELOG entry.

## Note

- The original (non-numeric) name `TICKET-render-pipeline-fps-defaults-audit`
  is retained as a **legacy audit-trail reference** for the code-review nit;
  the canonical doc uses the numeric `TICKET-095.md` per
  `DOCUMENTATION_GOVERNANCE.md`'s `TICKET-NNN.md` convention.
- This ticket is **non-blocking**: it is a code-review nit audit, not an
  infrastructure blocker.  The parent audit `TICKET-render-pipeline-fps-defaults`
  can close without further action.
- AGENTS.md v0.1 freeze Cat-1 (build corrections — install-pipeline plumbing).
  Zero new public symbols; pure audit closure.
- `render_scene` overloads are `@deprecated Fase C3` — future cleanup leaves
  this audit decision in place; the uniform-no-default policy is the desired
  steady state for the migration target `render_composition`.
