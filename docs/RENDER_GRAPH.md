# Render Graph

The render graph is the execution model for a single frame. Rather than rendering layers sequentially and mutating a shared framebuffer, each piece of rendering is expressed as a **node** in a directed acyclic graph. Edges carry framebuffers between nodes. The graph is built once per frame, then executed by `GraphExecutor`, which caches results by content hash.

---

## Why a graph

The old renderer applied effects, masks, and compositing in a fixed loop. Adding new behaviour (precomps, nested graphs, motion blur) required branching inside the loop. The graph model makes each operation an isolated, cacheable unit with explicit inputs and outputs.

---

## Namespaces

The render graph lives in `chronon3d::graph` and is the production execution model for the software renderer.

```
chronon3d::graph  ← graph nodes, executor, builder
```

---

## Node types

All nodes inherit from `RenderGraphNode` (`include/chronon3d/render_graph/render_graph_node.hpp`).

| Node | Kind | What it does |
|---|---|---|
| `ClearNode` | Output | Produces a fully transparent framebuffer. `cacheable() = false` — never cached. |
| `SourceNode` | Source | Renders one `RenderNode` (shape/text/image) into a framebuffer via `SoftwareRenderer`. |
| `MaskNode` | Mask | Clips pixels outside the mask to alpha=0. Coordinate origin follows the modular graph coordinate mode. |
| `EffectStackNode` | Effect | Applies an ordered `EffectStack` (blur, tint, brightness, contrast…) to its input. |
| `AdjustmentNode` | Adjustment | Same as EffectStack but semantically applied to everything below it in the layer stack. |
| `CompositeNode` | Composite | Alpha-composites two framebuffers (bottom + top) with a given `BlendMode`. |
| `PrecompNode` | Precomp | Evaluates a nested composition at a local frame, builds a nested graph, executes it. |

---

## Cache key

Every node produces a `cache::NodeCacheKey`:

```cpp
struct NodeCacheKey {
    std::string scope;      // e.g. "layer.source:card:bg"
    Frame       frame;
    i32         width, height;
    u64         params_hash;   // hash of node-specific params (shape, effects, mask…)
    u64         source_hash;   // hash of the node's identity (name/path)
    u64         input_hash;    // combined hash of all upstream resolved key digests
};
```

`input_hash` is injected by `GraphExecutor` at execution time. It chains the resolved digest of each input node (post-`input_hash` injection), so the full upstream state is reflected in every node's cache key.

Hashing utilities live in `include/chronon3d/render_graph/render_graph_hashing.hpp`. The key functions:

- `hash_render_node(const RenderNode&)` — covers name, transform, shape, color, shadow, glow
- `hash_shape(const Shape&)` — discriminates on `ShapeType` and hashes active variant fields
- `hash_effect_stack(const EffectStack&)` — ordered hash of enabled effects
- `hash_mask(const Mask&)` — type, pos, size, radius, inverted

---

## Graph execution

`GraphExecutor` (`include/chronon3d/runtime/graph_executor.hpp`) traverses the graph in topological order by recursion, memoizing results in `m_temp`.

For each node:

1. Execute all inputs recursively, building `inputs` vector and `input_hash`.
2. Compute the final `NodeCacheKey` including `input_hash`.
3. If `node.cacheable() && cache_enabled`: check the cache. On hit, return cached framebuffer and record the resolved digest.
4. Otherwise execute `node.execute(ctx, inputs)`.
5. Store the resolved digest in `m_resolved_key_digest[id]`.
6. If cacheable and result is non-null: store in cache.

`m_resolved_key_digest` stores the final key digest (after `input_hash` injection) for each node. Parent nodes use this map to build their own `input_hash`, making the hash chain fully recursive across the entire DAG.

---

## Graph construction

`GraphBuilder::build(const Scene& scene, const RenderGraphContext& ctx)` constructs the graph:

1. Start with a `ClearNode` as the canvas.
2. For each root `RenderNode` in the scene: add a `SourceNode` → `CompositeNode` chain.
3. For each `Layer` (bottom to top):
   - `LayerKind::Null` → skip
   - `LayerKind::Normal` → `build_layer_source()` (one `SourceNode` per `layer.nodes`) → optional `MaskNode` → optional `EffectStackNode` → `CompositeNode`
   - `LayerKind::Adjustment` → `AdjustmentNode` applied to the current composite
   - `LayerKind::Precomp` → `PrecompNode` → `CompositeNode`
4. Call `graph.set_output(current)` before returning.

`build_layer_source` creates a `ClearNode` as the layer's canvas and composites each node in `layer.nodes` onto it with `BlendMode::Normal`.

---

## RenderGraphContext

```cpp
struct RenderGraphContext {
    Frame frame;
    float time_seconds;
    int width, height;
    Camera camera;                  // passed to SoftwareRenderer::draw_node

    SoftwareRenderer* renderer;     // null-safe: nodes check before calling
    cache::NodeCache* node_cache;   // null = cache disabled for this context
    RenderProfiler*  profiler;      // null = no profiling
    CompositionRegistry* registry;  // for PrecompNode composition lookup

    bool cache_enabled;
    bool diagnostics_enabled;
    bool modular_coordinates;       // true when using centered modular graph coordinates
};
```

All pointers are nullable. Nodes check before dereferencing.

---

## Profiling

`RenderProfiler` (`include/chronon3d/render_graph/graph_profiler.hpp`) records per-node timing and cache hit/miss data. Call `begin_frame` / `end_frame` around each executor call, then use `get_summary()` for aggregate stats.

```cpp
RenderProfiler profiler;
profiler.begin_frame(ctx.frame);
auto result = executor.execute(graph, ctx);
profiler.end_frame();

auto summary = profiler.get_summary();
// summary.hit_rate, summary.avg_frame_time_ms, summary.slowest_nodes
```

---

## Output node

Every graph has an explicit output node set via `graph.set_output(id)`. Calling `graph.output()` on a graph without a set output throws `std::logic_error`. `GraphBuilder` always calls `set_output` before returning. `PrecompNode` uses `graph.has_output()` before executing nested graphs.

---

## Adding a new node type

1. Add a value to `RenderGraphNodeKind` in `render_graph_node.hpp`.
2. Create a class inheriting from `RenderGraphNode` in `include/chronon3d/render_graph/nodes/`.
3. Implement `kind()`, `name()`, `cache_key()`, and `execute()`.
4. Override `cacheable()` if the node should never be cached (e.g. time-varying sources without a stable hash).
5. Add the node to `GraphBuilder` where relevant.

Nodes must not hold raw pointers to scene data that could outlive the graph. Use value copies (as `SourceNode` does for `RenderNode`).

---

## Known limitations

- `MaskNode` applies masks in the graph's coordinate space. It does not apply the layer's world transform. Works correctly for identity-transform layers.
- `PrecompNode` does not propagate `ctx.camera` into the nested context — it reuses the parent camera.
- `use_modular_graph` is retained as a compatibility switch for coordinate mode only. The renderer always uses the modular graph execution path.
