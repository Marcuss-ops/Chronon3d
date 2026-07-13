# ADR-024: Deprecate per-node persistent framebuffer cache flag

## Status

PROPOSED — 2026-07-12.

Depends on landing first: `chore(runner): remove dead persistent-cache branch`
(point (3) of the action plan = current scope, removes the dead branch in
`src/render_graph/executor/node_executor.cpp::run_node`).

## Context

`RenderGraphNode::cache_policy()` returns an immutable `RenderNodeCachePolicy`
instance set at construction. The original comparator exposed a `.persistent()`
accessor (used by the legacy persistent-framebuffer-cache mechanism that was
deprecated when `framebuffer_store → framebuffer_pool` consolidation landed).
A helper function `persistent_framebuffer_cache_enabled_for_current_run()`
(in `src/render_graph/executor/cache_evaluator.{hpp,cpp}`) was retained to
drive an environment-level feature-flag check, but no `Config` field drives
it — the implementation returns `false` literally and unconditionally.

### Audit findings (post-dedup-via-ripgrep)

Audit via `rg -n "persistent_framebuffer_cache_enabled_for_current_run"` +
`rg -n "cache_policy\(\)\.persistent" .` revealed zero non-trivial real
callers after the dead-branch removal:

| Symbol | Definition | Real call sites (post-deprecation) |
|---|---|---|
| `persistent_framebuffer_cache_enabled_for_current_run()` | `cache_evaluator.cpp:13` (returns `false` always) | none (only in the removed dead branch) |
| `node.cache_policy().persistent()` | accessor on `RenderNodeCachePolicy` | none (only in the removed dead branch) |
| `policy.persistent()` (local var form) | same accessor, different expression | only `cache_evaluator.cpp:59` as `(void)policy.persistent();` (no branch decision — pure void-cast no-op result-discard). **Vestigial void-cast caller — explicitly IN step-3 cleanup scope**. |
| `disable_persistent_framebuffer_cache*` env vars + `framebuffer_store` (CFB4 codec) | `core/config.cpp:168,173` + `cache/persistent_framebuffer_store.cpp` (live) | ↗ **OUT OF SCOPE** — see "Scope clarification" below |

### Scope clarification: this ADR covers ONLY the per-node persistent-flag axis

The `disable_persistent_framebuffer_cache` env var + `persistent_framebuffer_cache_dir`
env var + the `framebuffer_store` module (CFB4 binary codec + disk I/O) remain ACTIVE.
They back a separate axis: cache Backing storage for the in-memory `framebuffer_pool`.
These two axes share the word "persistent" but are unrelated semantically. Their
deprecation would be a SEPARATE ADR (NOT this one).

## Decision

**Deprecate** (full removal in a post-ADR-acceptance follow-up commit) the
**per-node persistent-flag axis** only:

- `RenderNodeCachePolicy::persistent()` accessor on the type (defined in
  `include/chronon3d/render_graph/core/cache_policy.hpp`).
- `persistent_framebuffer_cache_enabled_for_current_run()` helper function
  (declaration: `cache_evaluator.hpp:10`; definition: `cache_evaluator.cpp:13`).
- The vestigial `(void)policy.persistent();` no-op call at
  `cache_evaluator.cpp:59` (and its preceding `// persistent framebuffer cache
  removed (framebuffer_store → framebuffer_pool)` comment marker).
- The forward-pointing doc comments at `cache_evaluator.cpp:11` and the
  `DEAD BRANCH PRESERVATION` block at `node_executor.cpp:9-18` (the latter is
  removed in step-1 commit body, this ADR captures the broader intent).

### Closure sequence

| Step | Subject | Purpose |
|---|---|---|
| 1 (current) | `chore(runner): remove dead persistent-cache branch` | Remove the specific empty `if` branch in `node_executor.cpp::run_node` (+ drop the now-fulfilled "DEAD BRANCH PRESERVATION" comment-block at top of file) |
| 2 | (this ADR) | Capture decision + scope + consequences + non-goal (preserve `framebuffer_store` CFB4 codec OUT of scope) |
| 3 (post-acceptance) | `chore(runner): deprecate per-node persistent cache flag (ADR-024)` | Drop **(a)** `RenderNodeCachePolicy::persistent()` accessor (defined in `include/chronon3d/render_graph/core/cache_policy.hpp`); drop **(b)** `persistent_framebuffer_cache_enabled_for_current_run()` helper function (decl `cache_evaluator.hpp:10`, defn `cache_evaluator.cpp:13`); drop **(c)** vestigial `(void)policy.persistent();` + comment markers in `cache_evaluator.cpp`; drop **(d)** `m_persistent` data-field in `RenderNodeCachePolicy` struct if present (audit-driven). Bisect-able if ADR is REJECTED post-commit. |

## Consequences

### Positive

- Removes dead/disabled code from production compile paths.
- Reduces API surface on `RenderNodeCachePolicy` (fewer unused accessors).
- Eliminates the `if (dead && dead-return-false) { /* never-runs */ }` anti-pattern.
- Lowers per-frame `cache_evaluator` overhead by removing the `(void)policy.persistent()`
  no-op (single-token result read + discard on every cache_write call path).

### Negative

- **Minor API churn** for any external consumer reading `.persistent()` accessor.
  None known — `RenderNodeCachePolicy` is graph-internal.
- Requires coordination if ADR is REJECTED post-acceptance → revert step-3 commit.

### Neutral

- The `framebuffer_store` (CFB4 disk codec) + `disable_persistent_framebuffer_cache` env
  stay unchanged (separate axis, out of scope per above).

## Alternatives considered

### A. Leave the dead branch as-is (status quo)

- **Pro:** zero change risk.
- **Con:** rot-class conflict (§honesty violation) — dead code in production
  compile paths produces false success signals in audit tooling.

**Rejected** — fails AGENTS.md §honesty + rotates on every git blame.

### B. Keep the helper function, gate on real config

- **Pro:** no breaking API change.
- **Con:** preserves dead feature-flag complexity for a feature that's hard-wired
  `false` in `cache_evaluator.cpp:13`. Future consumers would inherit the dead-on-arrival
  pattern.

**Rejected** — the helper returning `false` always is dead-by-construction.

### C. Move helper into `RenderNodeCachePolicy` itself (collapse the two-file axis into one)

- **Pro:** -1 surface (one fewer file modified per future change).
- **Con:** doesn't fix the fundamental rot; just relocates it. Decided against because
  the helper's purpose (env-gated feature flag) doesn't belong on the policy type
  (which is build-time-immutable per `RenderGraphNode` doc-comment at
  `render_graph_node.hpp:110`).

**Rejected** — relocation without disablement isn't a cleanup.

## Forward-points

- After step-3 commit lands, audit `RenderNodeCachePolicy` definition in
  `include/chronon3d/render_graph/core/cache_policy.hpp` to find any **other**
  dead accessors (history suggests this type has accumulated multiple accessors:
  `.frame_dependent()`, `.enabled()`, `.reusable_across_frames()` — each warrants
  audit for callers + non-trivial return-usage).
- Cross-link `docs/FOLLOWUP_TICKETS.md` Cita-Only entry per AGENTS.md §disciplina canonici:
  one line referencing this ADR + ticket-id (`TICKET-PERMANENT-CACHE-FLAG-DEPRECATION`).
  ≤10-line open-blockers policy applies.

## References

- `src/render_graph/executor/node_executor.cpp` lines 96-110 (dead branch — REMOVED in step 1)
- `src/render_graph/executor/cache_evaluator.cpp` lines 11, 13, 59 (helper decl-defn + vestigial call — REMOVED in step 3)
- `src/render_graph/executor/cache_evaluator.hpp` line 10 (helper declaration — REMOVED in step 3)
- `include/chronon3d/render_graph/nodes/render_graph_node.hpp` line 110 (`cache_policy()` accessor)
- `include/chronon3d/render_graph/core/cache_policy.hpp` (type definition — audit target for step 3)
- `src/core/config.cpp` lines 168, 173 (CFB4 config axis — **OUT OF SCOPE** for this ADR)
- `include/chronon3d/cache/persistent_framebuffer_store.hpp` (CFB4 codec — **OUT OF SCOPE**)

## Audit reproducibility

```bash
rg -n "persistent_framebuffer_cache_enabled_for_current_run" .
rg -n "cache_policy\(\)\.persistent" .
rg -n "policy\.persistent" .
```

All three queries post-step-1 commit return ≤1 match (the dead-branch whitespace
discharged by the `if`-removal str_replace), which makes the post-step-3 ADR
acceptance criteria straightforward to verify.
