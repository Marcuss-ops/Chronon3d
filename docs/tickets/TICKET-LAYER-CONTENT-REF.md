# TICKET-LAYER-CONTENT-REF — one content dimension, one predicate

**Status**: `PROPOSED` (design only; no code changed in this ticket)
**Owner**: maintainer (review on `main`)
**Related**: `docs/adr/ADR-031-layer-content-ref.md`, `docs/adr/ADR-029-authority-boundary-doctrine.md`,
`AGENTS.md` §Demolition Debt, `docs/tickets/TICKET-104*.md` (coordinates / kind restriction)

---

## 1. Problem, with evidence

`LayerKind` (`include/chronon3d/scene/model/layer/layer.hpp:36-48`, member at `:53`) is a ten-value
enum on the public scene model that answers **three different questions at once**:

| Question | Today's values |
|---|---|
| **What does it show?** (content) | `Normal`, `Shape`, `Text`, `Video`, (`Precomp`) |
| **What does it do?** (role) | `Null`, `Adjustment`, `Precomp`, `Camera`, `Audio` |
| **How is it treated?** (presentation) | `Glass` |

Because the content question has no single home, the same decision is re-made by hand wherever a
layer's content matters. **75 `LayerKind` occurrences in production code across 31 files**
(`grep -rn LayerKind src include`). The worst instance is one predicate spelled out five times:

| Site | Predicate |
|---|---|
| `src/render_graph/pipeline/scene_refresh.cpp:151` | `kind != Normal && kind != Shape && kind != Text` |
| `src/render_graph/pipeline/scene_refresh.cpp:172` | `kind != Normal && kind != Shape && kind != Text` |
| `src/render_graph/pipeline/refresh/source.cpp:67` | `kind == Normal \|\| Shape \|\| Text` |
| `src/render_graph/pipeline/refresh/multi_source.cpp:28` | `kind == Normal \|\| Shape \|\| Text` |
| `src/render_graph/builder/passes/graph_builder_source_pass.cpp:72` | `kind == Normal \|\| Shape \|\| Text` |
| `src/compiler/graph_builder/graph_builder_layer_pipeline.cpp:86` | `kind == Normal \|\| Shape \|\| Text \|\| Video` |
| `src/compiler/graph_builder/graph_builder_layer_pipeline.cpp:109` | `kind == Normal \|\| Shape \|\| Text \|\| Precomp \|\| Video` |

`source.cpp` and `multi_source.cpp` hold byte-identical predicate bodies in two files: a Cat-3
duplication ("una sede canonica per ogni informazione"). Against ADR-029 this trips **Resolver
Finality** (the same policy is re-derived downstream) and **Blast Radius** (a policy change touches
4+ modules).

The failure mode is recorded, not hypothetical: the coordinates builder keyed on `LayerKind::Normal`
and silently dropped Shape layers (see the `TICKET-104` comment retained at
`src/render_graph/builder/graph_builder_coordinates.hpp:96-103`, whose restriction was later removed
at the cost of leaving the enum read at `:210`). Adding one content kind today means editing every
chain; missing one silently removes a layer from refresh, bounds or compositing.

---

## 2. Target model

The layer keeps **one** content dimension and **one** role dimension, and the content dimension is
opaque to this engine — it is an id, never a text/font/glyph/raster type
(see the boundary doctrine: content is authored and resolved outside this engine).

```cpp
struct Layer {
    LayerId  id;
    LayerId  parent;

    ContentRef content;      // WHAT it shows  — NoContent | Image | Text | Video | Composition | …
    LayerRole  role;         // WHAT it does   — Rendered | Null | Adjustment | Camera | Audio

    TransformTracks transform;
    Track<float>    opacity;
    FrameRange      lifetime;
};
```

`ContentRef` is a variant of opaque asset ids plus `NoContent`; it has no opinion about what the
content *is*. Every consumer asks the layer, not the enum:

```cpp
layer.content().draws()        // carries drawable content
layer.content().kind()         // ContentKind::Image | Text | Video | Composition | …
layer.role()                   // Rendered | Null | Adjustment | Camera | Audio
```

`src/render_graph/pipeline/refresh/{source,multi_source}.cpp` both call the one predicate; the six
hand-written chains above disappear.

---

## 3. Taxonomy — proposed mapping (SIGN-OFF REQUIRED)

The content axis (`Text`, `Image`, `Video`, `NoContent`) is settled by the boundary design. The
Chronon3d-specific kinds below need the maintainer's decision before code moves.

| Today | Proposed `content` | Proposed `role` | Note |
|---|---|---|---|
| `Normal` | `Image` (raster content) | `Rendered` | "draws its own content"; the builder assigns the asset |
| `Shape` | `Shape` (shape asset) | `Rendered` | shape is content, owned by the content side |
| `Text` | `Text` | `Rendered` | settled by the boundary design |
| `Video` | `Video` | `Rendered` | settled by the boundary design |
| `Precomp` | `Composition` | `Rendered` | a nested composition *is* the layer's content — **confirm** |
| `Null` | `NoContent` | `Null` | settled: `Null = Layer + NoContent` |
| `Adjustment` | `NoContent` | `Adjustment` | settled: effects-only, no own content |
| `Glass` | *unchanged* | `Rendered` | **confirm**: a rendering treatment (material/effect), not a kind |
| `Camera` | `NoContent` | `Camera` | **confirm**: camera *authoring* belongs to the motion side; this engine consumes a compiled `CameraState` — second exit, see §6 |
| `Audio` | `Audio` | `Audio` | **confirm**: audio is an asset with no visual |

### Rejected alternative: a second `ContentKind` enum

Adding `ContentKind` beside `LayerKind` would leave the two questions split across two enums that
must be kept in sync — the duplication this ticket exists to remove, only harder to see.

---

## 4. Phases (each ends buildable, testable and shippable)

### P1 — additive: introduce the model, derive the old view

Add `ContentRef`, `LayerRole` and `Layer::content` / `Layer::role` plus the canonical predicates.
`LayerKind kind` **stays**, computed from `(content, role)`, so no reader changes in this phase.

*Exit*: `chronon3d_render_graph_tests` (378/378), `chronon3d_scene_tests`, `chronon3d_renderer_tests`,
`chronon3d_precomp_tests`, `chronon3d_visibility_contract_tests` and the `AGENTS.md` pre-push set
(`backend_registry`, `compositor`, `render_job_contract`, `render_graph`) PASS; canary artifact SHA
unchanged.

### P2 — migrate the readers

Replace the six hand-written chains (and the remaining `kind` reads, 75 sites / 31 files) with the
canonical predicate, file by file, running the touched module's suite per file.

*Exit*: zero `LayerKind` reads in `src/render_graph/**` and `src/compiler/**`; same artifact SHAs.

### P3 — remove the derived view

`kind` becomes non-public (or is removed outright), `LayerKind` shrinks to whatever survives as a
role-only enum, or disappears.

*Exit*: zero `LayerKind` occurrences in `src/` + `include/`; the array of identifiers in the ADR §
"removal scope" is empty.

### P4 — close the bridge

Delete the compatibility accessor and register one declarative rule in
`tools/architecture_rules.toml` (`[[scan]]`) that forbids a hand-written multi-way `LayerKind` chain
from returning. Specialistic gate (if any) registered once in `tools/gates/manifest.sh`.

*Exit*: gate registered and green; the rule is proven non-vacuous by mutation (inject a chain, the
gate fails).

---

## 5. Demolition Debt card (mandatory, per `AGENTS.md`)

| Field | Value |
|---|---|
| **Owner** | maintainer of `scene/model/layer/` |
| **Reason** | Migration P1–P3: `ContentRef` and `LayerKind` coexist so no single commit has to convert 75 sites across 31 files |
| **Exit condition** | Zero `LayerKind` occurrences in `src/` and `include/` (`grep -rn LayerKind src include` empty), and the P4 architecture rule green |
| **Equivalence test / gate** | `chronon3d_render_graph_tests` 378/378 + `chronon3d_scene_tests` + `chronon3d_renderer_tests` + the `AGENTS.md` pre-push set PASS **and** canary artifact SHA unchanged across P1→P3 — the bridge must be behaviour-neutral, and the SHA is what says so |
| **Removal scope** | the derived `Layer::kind` accessor, the `LayerKind` enum (or its role-only remnant), the compatibility includes in `scene/model/layer/layer.hpp`, and the P1 constructors that populate both representations |
| **Status** | `ACTIVE` (from P1) → `READY_TO_REMOVE` (end of P2) → `REMOVED` (P4) |

---

## 6. Second exit: `Camera`

Per the boundary doctrine, camera *authoring* belongs to the motion side and this engine consumes a
compiled `CameraState`. `LayerKind::Camera` (5 occurrences) is therefore not a content kind to
preserve — it is a candidate to leave this engine entirely. That migration is **out of scope here**
and tracked separately; this ticket only stops `Camera` from being encoded as content.

---

## 7. Explicit non-goals

- **No** text/font/glyph/raster type in this engine's layer model: content ids stay opaque.
- **No** second `LayerKind`-like discriminator.
- **No** change to `RenderGraphNodeKind` / `ProducerSurfaceKind`: those are the compiled graph's own
  vocabulary (what the compiler decided to execute), not scene-model content. This ticket touches
  `LayerKind` only.
- **No** percentage estimates: statuses are `PASS` / `FAIL` / `PARTIAL` / `NOT RUN`.

---

## 8. Open questions before P1

1. Taxonomy rows marked **confirm** in §3 (`Precomp`, `Glass`, `Camera`, `Audio`; and whether
   `Normal` maps to `Image` content or to `NoContent` until a builder assigns one).
2. Does `Glass` exist anywhere outside the enum (1 occurrence)? If not, P3 may delete it rather than
   carry it as a treatment flag.
3. `Audio` currently also implies a pre-warm hook; confirm the role value carries that behaviour.
