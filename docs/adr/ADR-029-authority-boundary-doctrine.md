# ADR-029 — Authority-Boundary Doctrine (project-agnostic design and review reference)

**Status**: PROPOSED (promotion to Accepted per `docs/adr/README.md` closure rule; the doctrine itself is prose guidance, so the validating signal is review usage on merged PRs rather than a dedicated CI gate — see *Consequences*).

## Date

2026-09-11

## Deciders

Project maintainers (review on `main`).

## Tags

`architecture`, `governance`, `review-process`, `ownership`

## Context

The repository already pins down *where* authority lives for the components
that exist today: invariant **I-1** in `docs/GOVERNANCE.md` ("una sola
authority per concetto"), the no-duplication invariants in `AGENTS.md`
(canonical registry/resolver/compiler/executor), the 3-family cache taxonomy,
and per-component ownership records such as `docs/EXECUTOR_OWNERSHIP.md`.

What is missing is the *general* doctrine used to evaluate any **new or
growing** component: a shared vocabulary for deciding when a component has
accumulated too much authority, when responsibilities genuinely belong
together, and when a proposed split is over-engineering. Until now this
reasoning lived in review conversations and tribal knowledge.

Two failure modes must be covered symmetrically:

1. **Concentrated authority** — one component decides, owns, synchronizes,
   executes and persists at the same time (god object / god service).
2. **Distributed or fragmented authority** — the same decision is re-made in
   several layers, or a trivial responsibility is shattered into five
   `*Helper`/`*Coordinator` classes that always change together.

Per the maintainers' decision, this ADR records the doctrine as **prose
reference only**: no mechanical gate is added in this change (see
*Alternatives considered*).

## Decision

Adopt the following doctrine as the standard design/review reference for this
project, and as the portable default for any other project. It is
project-agnostic on purpose: no Chronon component is named in the normative
text.

### Core principle

> **A component should have one clear, bounded authority. When it decides,
> owns, synchronizes, executes and persists too many things at once, it
> should be split.**

"Authority" means *the right to decide something* — not merely to touch it.
The doctrine reasons about who has the right to decide what, which is a
stronger lens than the plain Single Responsibility Principle.

### The 12 authority tests

Applied during design and review. Red flags are triggers for a conversation,
not automatic verdicts.

| Test | Question | Red flag |
|---|---|---|
| Authority Dimension | On how many dimensions does this component decide? | Policy + state + lifecycle + scheduling + storage at once |
| Fan-In | How many independent domains depend on it? | 5+ independent domains |
| Fan-Out | How many subsystems does it modify directly? | 3+ subsystems in the same method |
| Lock | How much concurrency passes through the same lock? | One global mutex on the data plane |
| Independent Work | Can two independent jobs proceed in parallel? | No, without a strong invariant |
| State Geography | In how many places is the same state modified? | More than one writer authority |
| Resolver Finality | After a decision is made, do other layers reinterpret it? | Policy duplicated downstream |
| Blast Radius | How many modules does a policy change touch? | 4+ modules |
| Construction | How many dependencies are needed to instantiate/test it? | Half the system |
| Hot Path | How many managers/registries/contexts does it consult per request? | Continuous service lookup |
| Failure Authority | Who decides what happens when something fails? | Error handling dispersed |
| Lifetime | Who creates/destroys the resources? | Many different authorities |

### Ordering metric (heuristic, not scientific)

```text
Authority Risk =
  fan_in
+ fan_out
+ 3 * global_locks
+ 4 * mutable_globals
+ 2 * state_machines_owned
+ 2 * policy_domains_owned
+ 2 * lifecycle_domains_owned
```

Use it only to *order* suspects for refactoring:

```text
0–8    normal
9–14   review
15–20  high risk
20+    probable god object / god authority
```

### The 5 questions (the most important test)

For every central class ask:

```text
Who decides?
Who owns?
Who synchronizes?
Who executes?
Who persists?
```

If the answer is always the same class, that is a strong signal. Example
decomposition of an over-authoritative service:

```text
Before (too much authority):
PaymentService
    decides the payment method
    stores transactions
    handles retries
    calls the payment provider
    handles webhooks
    sends email
    updates analytics

After (each authority bounded):
PaymentPolicy        decides
PaymentRepository    persists
PaymentExecutor      executes
RetryPolicy          handles retries
WebhookHandler       handles events
NotificationService notifies
```

The goal is not "more classes"; it is that **each authority has a clear
boundary**.

### Anti-fragmentation rule (the opposite failure)

Over-fragmentation is as harmful as concentration. This is over-engineering:

```text
UserValidator
UserValidatorHelper
UserValidationManager
UserValidationResolver
UserValidationCoordinator
```

for one simple validation. The rule is not "split" — it is:

> **Split when distinct invariants, lifecycles or contention domains exist.**

If two responsibilities always change together, share the same lifetime, and
have no independent concurrency, they can stay together.

Universal test before creating a new component — if **at least 3 of 5** hold,
it probably deserves its own authority:

```text
1. Can it change independently?
2. Does it have its own invariant?
3. Does it have its own lifecycle?
4. Does it have its own contention domain?
5. Can it be tested independently?
```

### Orchestrators coordinate; they do not own

An orchestrator may coordinate domains, but must **not own the internal
logic** of the domains it coordinates:

```text
OrderService         orchestration only

PricingEngine        owns pricing
InventoryAuthority   owns availability
PaymentGateway       owns payment execution
ShippingPlanner      owns shipping decisions
FraudPolicy          owns fraud decisions
```

The same shape applies to data pipelines (`SourceReader`, `Validator`,
`Transformer`, `BatchScheduler`, `RetryPolicy`, `ArtifactStore`, with the
pipeline as orchestration) and to agent/LLM systems (`Planner`,
`ToolResolver`, `Executor`, `MemoryStore`, `OutputValidator`, `RetryPolicy`,
`BudgetAuthority` — the agent orchestrates but is not the source of truth for
everything).

### Anti-"Manager" rule

Apply special review attention to types named:

```text
Manager
Context
Runtime
Service
Engine
Coordinator
Controller
Pipeline
Session
```

Not because these names are bad, but because they naturally accumulate
authority. Each time one grows, ask:

> Is it coordinating, or is it becoming the owner of the decisions of the
> components it coordinates?

If the second, stop it and extract the decision it has absorbed.

### Anti-service-locator rule

> **The closer to the hot path, the less you should know about the global
> container.**

Avoid, per request/frame:

```cpp
runtime.get_cache();
runtime.get_device();
runtime.get_codec();
runtime.get_registry();
```

Resolve dependencies ahead of execution instead, then run against an
already-resolved plan:

```cpp
PreparedExecutionPlan {
    DeviceHandle
    CodecHandle
    CacheHandle
}
```

### State-machine rule

> **A state machine must have a single writer authority.**

Not:

```text
Scheduler -> set Running
Worker    -> set Failed
API       -> set Completed
DB        -> infers status
```

But:

```text
JobStateMachine.transition(...)   // the only writer
```

and everyone else *requests* a transition.

### Policy rule

> **A policy must have exactly one home.**

If the question is "which backend to use?", there must be one canonical place
that decides. Authority spread across layers — "the API decides a bit, the
runtime decides a bit, the backend falls back, the worker corrects" — is as
dangerous as authority over-concentrated in one place.

### One-sentence test

When reading a class, complete the sentence: *"This class exists for…"*. If
the sentence becomes *"This class exists to manage X, but also Y, plus Z, and
coordinate A, and keep B…"*, there is almost always too much inside. A sound
authority is describable in one sentence, e.g.:

> `InventoryService` is the source of truth on item availability.
> `RenderScheduler` decides when and where a frame is executed.
> `ExecutionResolver` converts requirements and capabilities into an
> immutable plan.

### The formula to remember

```text
ONE DOMAIN
ONE AUTHORITY
ONE WRITER
ONE POLICY SOURCE

BUT NOT NECESSARILY
ONE LOCK
ONE THREAD
ONE CLASS
ONE QUEUE
```

## Consequences

### Positive

- A shared, project-agnostic vocabulary for design review, complementing
  I-1 (which states the invariant) with the method to *evaluate* new code
  against it.
- Gives the Demolition Debt discipline (AGENTS.md) a decision framework: the
  "Exit condition" of a bridge is typically the restoration of a single
  authority boundary.
- Directly applicable to the recurring decomposition decisions of this
  project (runtime vs executor vs backend, pool ownership, hot-path
  preparation vs execution) without naming them in the normative text.

### Negative / limitations

- The Authority Risk score is a heuristic; numbers order suspicions but do
  not prove anything. It must never be quoted as a compliance metric.
- Enforcement is review-time only. A reviewer who skips the doctrine gets no
  mechanical safety net for these specific risks (the existing declarative
  gates in `tools/architecture_rules*.toml` continue to cover the
  mechanically checkable subset).
- The naming watchlist is advisory and must not degenerate into forbidding
  words; a well-bounded `Session` type is fine.

### Neutral

- The doctrine is stable reference material: expect edits only when the tests
  or thresholds themselves change, not per-feature.

## Alternatives considered

- **Section inside `docs/GOVERNANCE.md`.** Rejected: GOVERNANCE owns the
  weekly ritual and audit-remediation state; a stable doctrine belongs in a
  dedicated ADR that GOVERNANCE and AGENTS.md can reference with one line.
- **Mechanical gates in `tools/architecture_rules*.toml`** (e.g. flag
  `*Manager`/`*Coordinator` growth, service-locator getters on hot paths,
  fan-in thresholds). Rejected for now: these heuristics produce false
  positives without semantic context, and the repo deliberately keeps its
  declarative gates high-precision. Revisit only if review discipline proves
  insufficient (e.g. a repeated god-authority incident).
- **Italian placement mirroring the original discussion.** Rejected: the
  doctrine is written to be portable across projects; English matches the
  existing ownership documents and maximizes reuse.
- **Do nothing (keep it as tribal knowledge).** Rejected: the doctrine was
  already being applied de facto in reviews; leaving it unwritten invites
  drift and makes onboarding harder.

## References

- `AGENTS.md` — no-duplication invariants, Demolition Debt, architecture
  rules registry.
- `docs/GOVERNANCE.md` — invariant I-1 ("una sola authority per concetto"),
  hotspot H-2 (global mutexes on the data plane), golden rule.
- `docs/EXECUTOR_OWNERSHIP.md` — precedent of a recorded single-authority
  decision.
- `docs/adr/README.md` — ADR numbering, status and closure rules.
- `tools/architecture_rules.toml` — declarative enforcement engine (unchanged
  by this ADR).
