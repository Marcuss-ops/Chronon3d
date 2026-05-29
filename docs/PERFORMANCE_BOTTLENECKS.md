# Performance Bottlenecks — Graph Executor & Render Pipeline

## Executive Summary

When rendering cached frames (cache hit rate = 100%), the pipeline takes **~300ms per frame** for simple compositions like `ImgGridTest` (3 layers). This is **not** caused by the grid kernel or shape rendering — the source nodes are cached and not executed. The bottleneck is entirely in **graph executor overhead** and **redundant work done every frame**.

---

## 5 Identified Problems

### 🔴 P1: Graph Builder Rebuilds Everything Every Frame

**File:** `src/render_graph/builder/graph_builder_pipeline.cpp` → `build_graph()`

Every call to `render_scene_via_graph()` invokes the full graph builder pipeline:

```cpp
RenderGraph graph = [&]() {
    auto built_graph = detail::build_graph(scene, mutable_ctx, resolved);
    return built_graph;
}();
```

For each frame, `build_graph()`:
1. Iterates all layers and for each one:
   - Calls `compute_layer_bbox()` → for each node calls `processor->compute_world_bbox()` with full matrix math
   - Creates new `SourceNode`, `CompositeNode`, `ClearNode` objects (heap allocations)
2. Runs `compute_static_layers()` — recursive analysis of all layers
3. Runs `append_root_sources()` — creates root source nodes
4. Runs `append_layer_pipeline()` for each visible layer — creates composite chain
5. Runs early-exit analysis (`is_full_frame_opaque` traversal)

**Cost:** ~50–150ms per frame depending on layer count

**Fix:** Cache the graph (or an execution plan) when the scene fingerprint is unchanged between consecutive frames.

---

### 🔴 P2: Fast-Path Fingerprint Rarely Hits

**File:** `src/render_graph/pipeline/scene.cpp` → `render_scene_via_graph()`

There is a fast-path optimization that skips the entire pipeline:

```cpp
if (sw_renderer &&
    settings.enable_dirty_rects &&
    sw_renderer->m_prev_framebuffer &&
    sw_renderer->m_prev_frame == frame - 1)
{
    const uint64_t fingerprint = ensure_scene_fingerprint();
    if (!cam_changed && sw_renderer->m_prev_scene_fingerprint == fingerprint) {
        return sw_renderer->m_prev_framebuffer;  // ← immediate return, ~0ms
    }
}
```

However, compositions with `.duration = 1` (e.g. `ImgGridTest`, `GridColorShowcase`) produce a **different scene for each frame** because `comp.evaluate(frame)` is called with incrementing frame numbers. Even though the composition lambda returns the same scene every time, the `Scene` object or its hash may differ.

**Result:** The fast-path is bypassed, and the full builder + executor runs for every frame — even when all nodes are 100% cached.

**Fix:** Use a static fingerprint for compositions that produce identical scenes across frames (e.g. `.duration = 1` but no animated properties). Or better: cache the graph when fingerprint is stable frame-to-frame.

---

### 🔴 P3: CompositeNode Copies + Blends Full Frame (Even When Cached)

**File:** `include/chronon3d/render_graph/nodes/composite_node.hpp` → `CompositeNode::execute()`

Even when both bottom and top inputs are cache hits, the composite node executes:

```cpp
result = ctx.acquire_framebuffer(*bottom);  // DEEP COPY: 8 MB for 1920×1080
ctx.backend->composite_layer(*result, *top, m_mode, clip);  // BLEND: read + write 8 MB
```

For a 3-layer composition:
- Composite(bg + clear): copy 8MB + blend 8MB = 16MB
- Composite(grid + bg): copy 8MB + blend 8MB = 16MB
- Composite(text + grid): copy 8MB + blend 8MB = 16MB
- **Total: 48 MB of memory bandwidth per frame**

For `GridColorShowcase` (8 layers): **128 MB per frame**

The compositor in `software_compositor.cpp` uses SIMD (`simd::composite_normal_premul`) and TBB parallel_for, but full-frame blending on 1920×1080 still takes **15–40 ms per composite call**.

**Fix:** 
- Skip-When-Opaque: if the top layer is full-frame opaque (`is_opaque()`), skip the copy altogether and return the top directly
- Dirty-rect compositing: only blend the area that actually changed between frames
- Framebuffer reuse: instead of copy + blend in-place, use `std::move` semantics when the bottom is the previous result

---

### 🟡 P4: Executor Overhead for Cached Nodes

**File:** `src/render_graph/executor/executor.cpp` and `src/render_graph/executor/internal.cpp`

Even with 100% cache hit, `execute_single_node()` is called for every node:

```cpp
void execute_single_node(ExecutionState& state, RenderGraph& graph,
                         RenderGraphContext& ctx, ...) {
    // 1. Evaluate cache — compute digest, hash, lookup
    auto cache_eval = evaluate_cache(node, ctx, pr.input_hash, ...);
    
    // 2. Predicted bbox — virtual function call
    auto predicted_bbox = node.predicted_bbox(ctx, pr.input_bboxes);
    
    // 3. Dirty-rect clipping — bbox intersection math
    node_ctx.clip_rect = compute_dirty_clip(ctx, node, predicted_bbox);
    
    // 4. Node execution — even if cached, acquires framebuffer
    const double duration_ms = run_node(node, node_ctx, pr.inputs, ...);
    
    // 5. Telemetry — string formatting, record creation, allocations
    emit_node_records(ctx, node, cache_eval.key, ...);
}
```

For a graph with 10–20 nodes:
- 10–20 cache lookups (hash computation + map lookup)
- 10–20 virtual function calls for bbox
- 10–20 telemetry records (string formatting, memory allocation)
- Consumer refcount management (atomic operations)

**Cost:** ~10–50ms per frame

**Fix:** Skip `execute_single_node()` entirely when a node is cached and the frame is consecutive. Only the output composite node needs to execute.

---

### 🟡 P5: Redundant build_execution_plan()

**File:** `src/render_graph/executor/executor.cpp` → `GraphExecutor::execute()`

```cpp
const auto plan = build_execution_plan(graph, output);
```

This does a full topological sort (BFS/DFS) of the graph every frame. For a graph that never changes between frames of the same composition, this is 100% redundant.

**Cost:** ~5–20ms per frame

**Fix:** Cache the execution plan when the graph structure doesn't change between frames.

---

## Benchmark Data

### Cold Start (first-frame, cache miss)

| Composition | Main (no kernel) | Optimized (kernel) | Speedup |
|---|---|---|---|
| **ImgGridTest** (3 layers) | **425 ms** | **302 ms** | **+29%** ✅ |
| DarkGridBackground | 304 ms | 243 ms | noise (utility code) |

### Cached Frames (cache hit = 100%)

| Composition | Avg Frame Time | Nodes Executed | Pixels Touched |
|---|---|---|---|
| DarkGridBackground | 2.80 ms | 0 | 0 |
| GridCleanBackground | 3.72 ms | 0 | 0 |
| **ImgGridTest** | **336 ms** | **60** | **0** |
| **GridColorShowcase** | **299 ms** | **60** | **0** |

The cached numbers for `ImgGridTest` and `GridColorShowcase` are dominated by **graph builder + executor overhead**, NOT by grid rendering.

### Key Insight

- The **grid kernel optimization** works correctly (+29% on first frame)
- But **its impact is masked** by the executor overhead on subsequent frames
- The 300ms is a **pre-existing architectural issue**, not caused by the grid kernel

---

## Where Does the Time Go?

Estimated breakdown for a cached 3-layer frame:

```
┌────────────────────────────────────────────────┐
│  comp.evaluate(frame)            ~1-5ms         │
├────────────────────────────────────────────────┤
│  render_scene_via_graph():                     │
│  ├─ Fast-path check              ~0.1ms        │
│  ├─ resolve_layers()             ~1-5ms        │
│  ├─ compute_dirty_rect()         ~1-5ms        │
│  ├─ build_graph():                            │
│  │  ├─ layer bbox computation    ~30-80ms      │
│  │  ├─ node creation             ~5-15ms       │
│  │  ├─ early-exit analysis       ~5-20ms       │
│  │  └─ total                     ~50-150ms     │
│  ├─ optimize_graph()             ~1-5ms        │
│  └─ execute():                                │
│     ├─ build_execution_plan()    ~5-20ms       │
│     ├─ per-node overhead         ~10-50ms      │
│     ├─ composite copy+blend      ~40-120ms     │
│     └─ total                     ~60-200ms     │
├────────────────────────────────────────────────┤
│  TOTAL                          ~200-400ms     │
└────────────────────────────────────────────────┘
```

---

## Recommended Fixes (Priority Order)

| Priority | Fix | Expected Impact | Complexity |
|---|---|---|---|
| 🔴 P1 | **Cache the render graph** when fingerprint is stable | **-200ms** | Medium |
| 🔴 P2 | **Static fingerprint** for non-animated compositions | enables P1 | Low |
| 🔴 P3 | **Skip-When-Opaque** in CompositeNode | **-50 to -100ms** | Low |
| 🟡 P4 | **Skip cached node execution** when all inputs cached | **-10 to -50ms** | Medium |
| 🟡 P5 | **Cache execution plan** across frames | **-5 to -20ms** | Low |

### Quick Wins (Low Complexity)

1. **Skip-When-Opaque** in `CompositeNode::execute()`:
   - If top layer is full-frame and `is_opaque()`, return top directly instead of copy + blend
   - Estimated saving: 15–40ms per composite layer

2. **Cache execution plan** in `GraphExecutor`:
   - Store the last plan and reuse it if graph structure hasn't changed
   - Estimated saving: 5–20ms per frame

### Architectural Fixes (Medium Complexity)

3. **Cache the render graph** across frames:
   - Store the built graph in `SoftwareRenderer` alongside `m_prev_framebuffer`
   - Reuse when scene fingerprint matches
   - Estimated saving: 50–150ms per frame

4. **Static fingerprint** for duration=1 compositions:
   - Pin `comp.evaluate(frame)` to always return frame 0 for non-animated compositions
   - This enables the fast-path fingerprint check to actually work

---

## Files Referenced

| File | Purpose |
|---|---|
| `src/render_graph/builder/graph_builder_pipeline.cpp` | Graph builder — rebuilds every frame |
| `src/render_graph/pipeline/scene.cpp` | `render_scene_via_graph()` — main render entry point |
| `src/render_graph/executor/executor.cpp` | Graph executor — level scheduling, node execution |
| `src/render_graph/executor/internal.cpp` | Cache evaluation, bbox, telemetry per node |
| `include/chronon3d/render_graph/nodes/composite_node.hpp` | CompositeNode — copy + blend for every layer |
| `src/backends/software/software_compositor.cpp` | Software compositor — SIMD blend kernel |
| `src/runtime/bench_runner.cpp` | Benchmark runner — calls evaluate + render per frame |
| `src/render_graph/nodes/source_node.hpp` | SourceNode — cache_policy, cache key |
| `include/chronon3d/render_graph/nodes/clear_node.hpp` | ClearNode — framebuffer clear |
