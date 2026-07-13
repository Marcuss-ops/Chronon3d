# ADR-024 — composite_node atomic-counter hidden singleton

**Status**: PROPOSED

## Contest
- `include/chronon3d/render_graph/nodes/composite_node.hpp:109` contains
  `static inline std::atomic<u64> s_counter{0};`, a hidden singleton counter
  for assigning stable `CompositeNode` IDs across the lifetime of a `RenderGraph`.
- AGENTS.md `static\s+inline\s+\w+` audit (2026-07-13 F7 / Design Step 1)
  flagged this as the SINGLE verbatim-pattern match across
  `include/chronon3d/**/*.hpp`.
- Per AGENTS.md "Cat-3 minimal: prefer DELETE over WRAP" + "No nuovi
  singleton/registry/resolver/cache senza ADR", this finding requires ADR +
  decision before any commit that preserves the singleton.

## Decisione
Per AGENTS.md "Cat-3 minimal: prefer DELETE over WRAP", the chosen path is
**DELETE + dependency injection**: extract `s_counter` into per-instance
state passed by reference to `CompositeNode` (or co-located with the
constructor's existing `RenderGraph&` context).

Rationale:
- Single hidden singleton matches user verbatim regex.
- DI-eligible: the ID counter is logically a property of the *consumer*'s
  ID-space, not the *node type*.
- Performance-equivalent: std::atomic already pays the cost; moving it from
  file-scope to instance scope does not affect throughput (one atomic load
  + increment per `CompositeNode` construction).
- ABI-additive: we can preserve behavior by introducing an optional
  `Counter&` constructor parameter and falling back to a per-instance
  counter if absent.

## Alternative considerate
- ALT-A: **ADR + REGISTRY** (preserve as file-scope atomic, document as
  "node ID registry"). Rejected per AGENTS.md "Cat-3 minimal: prefer DELETE
  over WRAP" + the singleton's call pattern is single-call-site per
  construction, which DI handles more cleanly.
- ALT-B: **WRAP** (introduce `CompositeNode::next_id()` accessor). Discouraged
  per AGENTS.md Cat-3 — wrapping defers the real decision (DELETE-vs-REGISTRY)
  and creates a new ABI surface that compounds the rot pattern.
- ALT-C: **DEFER + ADR** (keep s_counter for now, document as "load-bearing
  legacy"). Rejected because the per-construction cost is fixed and the DI
  cost is bounded; deferring accumulates the same cross-session correctness problem.

## Conseguenze

POS:
- Per AGENTS.md §Fare PR minimale + Cat-3: 1 atomic chore removes a true
  hidden singleton.
- Net surface: removes 1 symbol from `include/chronon3d/render_graph/nodes/`,
  adds 1 `Counter&` constructor parameter (ABI-additive, backward-compatible).
- Cross-cutting observability: per-instance counter is testable; file-scope
  atomic is not (no instance to mock).

NEG:
- ABI-additive: existing `CompositeNode(...)` call sites MUST be updated to
  pass `Counter&` (or use default fallback path).
- Per-construction cost remains (atomic increment is essentially free on x86/x64;
  on weaker ARM cores it's 1-2 cycles; not a perf regression).

## Cross-references
- AGENTS.md: §Regole di lavoro ("Ogni nuova feature deve usare il registry,
  resolver o sampler canonico già esistente") + §Fare PR piccole e mirate
  + Cat-3 minimal-surface.
- AGENTS.md §Post-push SHA-selfcheck: chore commit requires SHA-triple equality
  verify.
- F7/Design-Step-1 audit: `docs/tickets/TICKET-DESIGN-STEP1-ISOLATE-DECOUPLE-83a0c4f.md` §2.2.

## Owner / Date
- Owner: TBD (post-F2 recovery agent)
- Date: 2026-07-13 (audit) / TBD (commit)
