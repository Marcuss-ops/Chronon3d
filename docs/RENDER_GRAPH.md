# Render Graph Specification

The Chronon3d Render Graph (`chronon3d::graph`) is a directed acyclic graph (DAG) representing the rendering pipeline for a single frame.

## Design Goals
1. **Caching**: Avoid redundant pixel work by hashing node inputs and parameters.
2. **Modularity**: Separate source rendering, effects, and compositing into reusable nodes.
3. **Parallelism**: Enable the `GraphExecutor` to process independent branches in parallel.

## Node Types
- **SourceNode**: Renders a primitive (Shape, Image, Text) into a new Framebuffer.
- **EffectNode**: Applies a post-processing effect (Blur, Tint, etc.) to an input Framebuffer.
- **CompositeNode**: Blends two or more Framebuffers using a `BlendMode`.
- **AdjustmentNode**: Applies a stack of effects to the accumulated result.
- **PrecompNode**: Executes a nested render graph (sub-composition).

## Caching Strategy
Caching is handled by the `GraphExecutor` using `chronon3d::cache::NodeCache`.

### NodeCacheKey
Each node generates a key based on:
- **Scope**: Node type identifier.
- **Frame**: Current timeline position.
- **Dimensions**: Target width and height.
- **Params Hash**: Hash of the node's internal state (e.g., shape properties, effect radius).
- **Input Hash**: Combined hash of the keys of all predecessor nodes.

**Crucial**: The `input_hash` ensures that if an upstream node changes, all downstream nodes are invalidated, preventing stale results.

## Coordinate Systems
The modular graph handles two distinct coordinate conventions:
1. **Screen Space (2D)**: Top-left origin (0,0). Used for standard UI-like layers.
2. **Projected Space (2.5D)**: Centered origin. Used when `enable_3d(true)` is set on a layer. 

`SourceNodes` are responsible for applying the correct offsets based on the layer's projection mode to ensure consistent positioning across the graph.
