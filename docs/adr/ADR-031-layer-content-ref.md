# ADR-031 — `ContentRef`: one content dimension on `Layer`

**Status**: PROPOSED
**Date**: 2026-09-17
**Deciders**: project maintainers (review on `main`)
**Tags**: `architecture`, `scene-model`, `public-sdk`, `layer`, `content-ref`, `demolition-debt`

## Context

`Layer::kind` (`LayerKind`, `include/chronon3d/scene/model/layer/layer.hpp:36`) is a ten-value
enum on the public scene model that answers three unrelated questions with one value: **what a
layer shows** (`Normal`, `Shape`, `Text`, `Video`), **what it does** (`Null`, `Adjustment`,
`Precomp`, `Camera`, `Audio`) and **how it is treated** (`Glass`).

Two consequences are already visible in the tree, and both are recorded rather than predicted:

1. **Policy is re-derived downstream.** The question "does this layer carry content that refresh
   must visit?" is hand-written in five places, four of them spelling the same chain
   (`scene_refresh.cpp:151,172`; `refresh/source.cpp:67`; `refresh/multi_source.cpp:28`;
   `graph_builder_source_pass.cpp:72`; plus `graph_builder_layer_pipeline.cpp:86,109`), with
   `source.cpp` and `multi_source.cpp` holding byte-identical predicate bodies in two files.
   Against ADR-029 this is a **Resolver Finality** red flag (the same decision re-made in several
   layers) inside a **Blast Radius** of 4+ modules: 75 `LayerKind` occurrences across 31 production
   files.
2. **A missed chain silently drops a layer.** The coordinates builder keyed on
   `LayerKind::Normal` and dropped Shape layers, so `bg_grid` never reached the graph; the
   restriction had to be removed and the enum read is still there
   (`graph_builder_coordinates.hpp:96-103,210`). The failure is silent, not a compile error.

Removing a public member of the scene model is a surface change on `include/chronon3d/`, which
`AGENTS.md` requires an ADR for (the same reasoning as ADR-028 for the composition registry).

## Decision

Adopt `ContentRef` as the **single** representation of layer content, and split the layer into two
orthogonal dimensions:

1. **Content** — `ContentRef`, a variant of opaque content ids plus `NoContent`
   (`Image`, `Text`, `Video`, `Composition`, `Audio`, …). It is the layer's answer to "what do I
   show?", and it deliberately carries **no** text, font, glyph, layout or raster type: content is
   authored and resolved outside this engine, which only ever holds an id and derived metrics
   (`ContentMetrics` bound to the asset fingerprint).
2. **Role** — `LayerRole` (`Rendered`, `Null`, `Adjustment`, `Camera`, `Audio`), the behavioural
   axis that is *not* content. `Null = Layer + NoContent` is the canonical spelling, not a content
   kind.
3. **One canonical predicate.** Consumers ask the layer —
   `layer.content().draws()`, `layer.content().kind()`, `layer.role()` — instead of writing their own
   multi-way `kind` chains. The five hand-written predicate sites above collapse into one owner.

Migration is phased and behaviour-neutral at every step; `LayerKind` survives as a **derived**
compatibility view until no reader is left, under the Demolition Debt card in
[`TICKET-LAYER-CONTENT-REF`](../tickets/TICKET-LAYER-CONTENT-REF.md) §5.

## Doctrine check (ADR-029)

| Test | Before | After |
|---|---|---|
| Resolver Finality | The content question is re-decided in 5 sites | One predicate on `Layer` |
| Blast Radius | Adding a content kind touches every chain across 4+ modules | Adding a content kind touches `ContentRef` and its parser |
| State Geography | "Which layers are source-bearing" lives in the graph builder, the refresh pipeline and the source pass | Lives on the layer |
| Authority Dimension | `LayerKind` decides content + role + presentation | One dimension per field |

## Consequences

**Positive.** One place owns "what content is this"; the graph builder, refresh, hasher, bounds
collector and software diagnostics ask it instead of re-deriving it; a new content kind (today's
`Composition`, tomorrow an external 3D surface) is an addition to a variant rather than an audit of
every chain; the content axis becomes opaque ids, which is the boundary the content/motion split
requires.

**Negative / cost.** A transition period with two representations (`content`/`role` plus the derived
`kind`), bounded by the Demolition Debt card; `Layer` grows by one variant field; scene builders
(`scene/builders/detail/scene_builder_layers.inl`, `layer_builder_*.cpp`) and the scene validator
must populate both dimensions during the transition.

**Neutral.** `RenderGraphNodeKind` and `ProducerSurfaceKind` are untouched: those describe what the
**compiler decided to execute**, which is this engine's own vocabulary and not scene-model content.

## Alternatives considered

1. **Keep `LayerKind`, add a documented naming convention** for which values are content. Rejected:
   the chains are the problem, not their names; a convention is not enforceable and the fifth chain
   will be written anyway.
2. **Add a second `ContentKind` enum beside `LayerKind`.** Rejected: two enums that must stay in
   sync is the same duplication with an extra failure mode — and it violates the one-authority
   invariant in `docs/GOVERNANCE.md` I-1.
3. **Full replacement in one change (75 sites, 31 files).** Rejected for this tree: it cannot be
   reviewed or bisected, and the golden artifact SHA would have 75 chances to move in one commit.
4. **Move text/imaging types into `ContentRef`** (e.g. a `TextStyle` on the layer). Rejected: it
   recreates a second text authority here, which is exactly what the content/motion boundary
   forbids.

## Forward-points / open questions

1. Taxonomy sign-off for the Chronon3d-specific kinds (`Precomp`, `Glass`, `Camera`, `Audio`, and
   `Normal`'s default content) — `TICKET-LAYER-CONTENT-REF` §3.
2. `LayerKind::Camera` is a candidate to leave this engine entirely (camera authoring belongs to the
   motion side; this engine consumes a compiled `CameraState`). Tracked separately, explicitly out
   of scope here.
3. P4 adds one declarative rule to `tools/architecture_rules.toml` forbidding a hand-written
   multi-way `LayerKind` chain from returning, proven non-vacuous by mutation.
