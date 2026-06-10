================================================================================
  TIER 1 — GAME CHANGERS
================================================================================

  1. PARALLEL TILE RENDERING (V3 Architecture)
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    Every node produces a full-frame Framebuffer (1920×1080 = 8 MB per buffer).
    Even with dirty-rect clipping, the entire buffer is allocated and the
    executor runs per-node, per-level, sequentially within each level. With 10+
    nodes, only a few execute in parallel (one per level). Most cores sit idle.

  Solution:
    Divide the canvas into a grid of tiles (e.g. 256×256 px). Each node becomes
    a tile-producing operation. The executor schedules tiles across all available
    cores via TBB nested parallel_for. Tiles that don't change are skipped.

  Files to create:
    - include/chronon3d/render_graph/tile_grid.hpp
        TileGrid struct, TileId, TileState (clean/dirty/static)
    - include/chronon3d/render_graph/tile_scheduler.hpp
        TileScheduler — distributes tile work across worker threads
    - src/render_graph/executor/tile_scheduler.cpp

  Files to modify:
    - src/render_graph/executor/graph_executor_phases.cpp
        Replace level-based scheduling with tile-based scheduling
    - include/chronon3d/render_graph/render_graph_node.hpp
        Add optional TileGrid& execute() overload
    - src/backends/software/software_compositor.cpp
        Add composite_tile() for tile-level compositing

  Implementation steps (ordered):

  Step 1.1 — Define TileId and TileGrid
  ```cpp
  // include/chronon3d/render_graph/tile_grid.hpp
  struct TileId {
      int tx, ty;
      bool operator==(const TileId& o) const = default;
      // hash support for unordered_map
      struct Hash { size_t operator()(TileId id) const { ... } };
  };

  struct TileRect {
      int tx0, ty0, tx1, ty1;  // inclusive range
      int count() const { return (tx1-tx0+1)*(ty1-ty0+1); }
  };

  struct TileGrid {
      int tiles_x, tiles_y;       // e.g. 8 tiles for 1920px at 256px/tile
      int tile_w, tile_h;         // typically 256×256
      int canvas_w, canvas_h;     // full canvas dimensions

      // Per-tile storage: only allocate tiles that are touched
      std::unordered_map<TileId, OwnedFB, TileId::Hash> tiles;

      // Acquire a tile (lazy allocation from pool)
      OwnedFB acquire_tile(TileId id, FramebufferPool& pool);

      // Merge all tiles into a full-frame framebuffer
      void merge_to(Framebuffer& dst, const std::optional<BBox>& clip);

      // Mark which tiles are dirty for this frame
      void mark_dirty(const BBox& region);

      // Count of non-empty tiles
      size_t active_tile_count() const;
  };
  ```

  Step 1.2 — Define TileMask (evolve existing DirtyRectMask)
  ```cpp
  // include/chronon3d/core/dirty_rect_mask.hpp (extend)
  class TileMask {
      int m_tiles_x, m_tiles_y;
      std::vector<bool> m_mask;  // dynamic size, supports 4K+

      void mark_tile(int tx, int ty);
      void mark_bbox(const BBox& bbox);

      // Propagate tile changes through the graph
      void propagate_from_inputs(std::span<const TileMask> input_masks);
      TileMask dilated(int radius_in_tiles) const;  // for blur/bloom
      TileMask intersect_with(const BBox& predicted) const;

      bool is_tile_affected(int tx, int ty) const;
      bool is_empty() const;
      std::optional<BBox> to_bbox() const;  // approximate union

      // Count affected tiles
      int affected_count() const;
  };
  ```

  Step 1.3 — Tile Scheduling (TBB-first approach)
  ```cpp
  // src/render_graph/executor/tile_scheduler.cpp
  void execute_tiled(
      ExecutionState& state,
      const CompiledPlan& plan,
      TileGrid& grid,
      RenderGraphContext& ctx)
  {
      // For each level in the plan
      for (auto& level : plan.levels) {
          // Parallelize across nodes in this level
          tbb::parallel_for(
              tbb::blocked_range<int>(0, level.size()),
              [&](auto& range) {
                  for (int i = range.begin(); i < range.end(); ++i) {
                      auto& node = graph.node(level[i]);

                      // Get the active tiles for this node
                      auto tile_mask = node.compute_tile_mask(ctx);

                      if (tile_mask.is_empty()) {
                          continue;  // skip node entirely
                      }

                      // Nested parallel_for across tiles
                      tbb::parallel_for(
                          tbb::blocked_range<int>(0, tile_mask.affected_count()),
                          [&](auto& tr) {
                              for (int ti = tr.begin(); ti < tr.end(); ++ti) {
                                  auto tile_id = tile_mask.affected_tile(ti);
                                  node.execute_tile(tile_id, grid, ctx);
                              }
                          }
                      );
                  }
              }
          );
      }
  }
  ```

  Step 1.4 — Composite tile compositing
  ```cpp
  // src/backends/software/software_compositor.cpp
  void composite_tile(
      Framebuffer& dst_tile,      // tile-sized framebuffer
      std::span<const Framebuffer*> src_tiles,  // tile from each layer
      std::span<const BlendMode> modes,
      const TileMask& active_mask)
  {
      // Only composite if tile is affected
      if (!active_mask.is_tile_affected(dst_tile.tile_id().tx, dst_tile.tile_id().ty)) {
          return;
      }

      // Accumulate blends in place
      for (size_t i = 0; i < src_tiles.size(); ++i) {
          if (modes[i] == BlendMode::Normal) {
              composite_normal_premul(dst_tile, *src_tiles[i]);
          } else {
              blend_mode_dispatch(dst_tile, *src_tiles[i], modes[i]);
          }
      }
  }
  ```

  Expected impact: 4-8× speedup on dynamic scenes with multiple layers/effects.
  Complexity: High (2-3 weeks)

  ──────────────────────────────────────────────────────────────────────────────

  2. FRAME GRAPH COMPILER (Compiled Display List)
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    Every frame calls build_graph() which creates new RenderGraphNode objects
    (heap allocations), computes layer bboxes, runs early-exit analysis, and
    builds the full graph. Even with the incremental cache, the first frame of
    each composition pays this overhead. Additionally, every execute() call is
    a virtual dispatch — 16+ vtable calls per frame.

  Solution:
    Compile the scene into a CompiledDisplayList once. This is a flat,
    immutable, cache-friendly representation with no virtual dispatch. Each
    node becomes an entry in a function pointer table indexed by NodeKind.
    The executor simply iterates a linear array and calls the right function.

  Files to create:
    - include/chronon3d/render_graph/display_list.hpp
        CompiledDisplayList, CompiledNode, CompiledEdge
    - src/render_graph/display_list_compiler.cpp
        DisplayListCompiler — Scene → CompiledDisplayList

  Files to modify:
    - src/render_graph/executor/graph_executor_phases.cpp
        Add fast path for CompiledDisplayList
    - include/chronon3d/render_graph/render_graph_node.hpp
        Add NodeKind enum to base class
    - src/runtime/scene_to_render_graph.cpp
        Integration point

  Implementation steps:

  Step 2.1 — Define the intermediate representation
  ```cpp
  // include/chronon3d/render_graph/display_list.hpp

  enum class NodeKind : uint8_t {
      Source,
      MultiSource,
      Composite,
      Transform,
      EffectStack,
      Adjustment,
      Clear,
      Mask,
      Lighting,
      Shadow,
      Precomp,
      Video,
      TrackMatte,
      Transition,
      DOF,
      MAX_KINDS
  };

  struct CompiledNode {
      NodeKind kind;
      uint32_t input_offset;    // index into edges array
      uint32_t input_count;
      uint32_t output_level;    // execution level (topological order)
      uint32_t params_hash;     // for cache lookup
      // Compact params storage — reinterpret_cast based on kind
      alignas(16) uint8_t params_storage[64];
  };

  struct CompiledEdge {
      uint32_t src_node;
      uint32_t dst_node;
      uint8_t src_output;       // for multi-output nodes
  };

  struct CompiledDisplayList {
      std::span<const CompiledNode> nodes;
      std::span<const CompiledEdge> edges;
      std::span<const uint32_t> levels;      // execution levels flattened
      int width, height;
      int tile_size;
      // Function pointer table indexed by NodeKind
      using ExecFn = void(*)(const CompiledNode&, ExecutionState&, RenderGraphContext&);
      static constexpr ExecFn dispatch_table[static_cast<int>(NodeKind::MAX_KINDS)] = {
          &execute_source_node,
          &execute_multi_source_node,
          &execute_composite_node,
          // ... etc
      };
  };
  ```

  Step 2.2 — Implement the compiler
  ```cpp
  // src/render_graph/display_list_compiler.cpp
  class DisplayListCompiler {
  public:
      CompiledDisplayList compile(const Scene& scene, int width, int height);

  private:
      CompiledNode compile_node(const Layer& layer, const ResolvedLayer& resolved);
      void build_execution_levels();
      void flatten_params(const EffectStack& effects, uint8_t* storage);
      uint32_t intern_string(std::string_view name);  // for node names

      std::vector<CompiledNode> m_nodes;
      std::vector<CompiledEdge> m_edges;
      std::vector<uint32_t> m_levels;
      std::vector<char> m_string_pool;
  };
  ```

  Step 2.3 — Integration into executor
  ```cpp
  // src/render_graph/executor/graph_executor_phases.cpp
  FramebufferRef GraphExecutor::execute(
      RenderGraph& graph,
      GraphNodeId output,
      RenderGraphContext& ctx)
  {
      // Fast path: use compiled display list if available
      if (m_compiled_list.has_value() && !m_graph_structure_changed) {
          return execute_compiled(*m_compiled_list, output, ctx);
      }

      // Slow path: full build + execute (legacy)
      const auto plan = build_execution_plan(graph, output);
      return execute_planned(graph, plan, output, ctx);
  }

  FramebufferRef GraphExecutor::execute_compiled(
      const CompiledDisplayList& list,
      GraphNodeId output,
      RenderGraphContext& ctx)
  {
      // Linear execution — no virtual dispatch, no hash tables
      for (uint32_t level_idx = 0; level_idx < list.levels.size(); ++level_idx) {
          uint32_t node_idx = list.levels[level_idx];
          const auto& node = list.nodes[node_idx];

          // Dispatch via function pointer table — single indirect call
          CompiledDisplayList::dispatch_table[static_cast<int>(node.kind)](
              node, m_state, ctx
          );
      }

      return m_state.temp[output]->ref();
  }
  ```

  Step 2.4 — NodeKind enum and per-node executors
  ```cpp
  // include/chronon3d/render_graph/render_graph_node.hpp
  class RenderGraphNode {
  public:
      virtual NodeKind kind() const = 0;  // <-- NEW
      // ... rest stays the same ...
  };
  ```

  Expected impact: 30-40% speedup on dynamic frames. Build graph from ~1-2ms to ~0ms.
  Complexity: High (2-3 weeks)

  ──────────────────────────────────────────────────────────────────────────────

  3. TILE SURFACE CACHE (Zero-Alloc Runtime)
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    Every frame, the FramebufferPool allocates and releases framebuffers via
    atomic operations (acquire/release with refcount). Even with the shared_ptr
    refactor, each temporal framebuffer still goes through pool acquire/release
    cycles — atomic ops in the hot path. Tile surfaces are re-allocated even
    when their content hasn't changed.

  Solution:
    Persistent tile surfaces that live for the entire composition lifetime.
    Each tile has a fixed surface in the TileSurfaceCache. Writes are
    copy-on-write (if shared). Reads are direct pointer access with zero
    overhead. Tile surfaces are only cleared when marked dirty.

  Files to create:
    - include/chronon3d/cache/tile_surface_cache.hpp
        TileSurfaceCache class
    - src/cache/tile_surface_cache.cpp

  Files to modify:
    - include/chronon3d/render_graph/render_graph_node.hpp
        Add TileSurfaceCache& to RenderGraphContext
    - src/render_graph/executor/graph_executor_phases.cpp
        Integrate tile surface cache into execute loop

  Implementation steps:

  Step 3.1 — Define TileSurfaceCache
  ```cpp
  // include/chronon3d/cache/tile_surface_cache.hpp
  class TileSurfaceCache {
  public:
      static constexpr int MAX_TILE_SURFACES = 4096;  // 64×36 for 4K

      struct TileSurface {
          OwnedFB fb;
          TileId id;
          bool dirty{true};         // needs recomposite
          bool is_static{false};    // hasn't changed in N frames
          int frame_last_modified{0};
          int ref_count{1};         // shared readers increment this
      };

      // Pre-allocate all tile surfaces
      void preallocate(int tiles_x, int tiles_y, int tile_w, int tile_h,
                      FramebufferPool& pool);

      // Get writable surface (copy-on-write if shared)
      Framebuffer& acquire_writable(TileId id);

      // Get readable surface (shared, no copy)
      const Framebuffer* acquire_readable(TileId id) const;

      // Mark tiles as dirty
      void mark_dirty(const TileMask& mask);

      // Clear only dirty tiles
      void clear_dirty_tiles(const TileMask& mask);

      // Mark tiles as static after N unchanged frames
      void update_static_tiles(int current_frame, int threshold = 3);

      // Stats
      size_t total_surfaces() const;
      size_t shared_surfaces() const;    // tiles with ref_count > 1
      size_t static_surfaces() const;    // tiles not modified recently

  private:
      std::unordered_map<TileId, TileSurface, TileId::Hash> m_surfaces;
      int m_tiles_x{0}, m_tiles_y{0};
      int m_tile_w{256}, m_tile_h{256};

      // Copy-on-write: clone surface if ref_count > 1
      void copy_on_write(TileId id);
  };
  ```

  Step 3.2 — Copy-on-Write implementation
  ```cpp
  // src/cache/tile_surface_cache.cpp
  Framebuffer& TileSurfaceCache::acquire_writable(TileId id) {
      auto it = m_surfaces.find(id);
      if (it == m_surfaces.end()) {
          // First allocation — create new surface
          auto [new_it, inserted] = m_surfaces.emplace(id, TileSurface{
              .fb = OwnedFB(new Framebuffer(m_tile_w, m_tile_h)),
              .id = id,
              .dirty = true
          });
          return *new_it->second.fb;
      }

      // Surface exists — copy-on-write if shared
      if (it->second.ref_count > 1) {
          copy_on_write(id);
          it = m_surfaces.find(id);
      }

      it->second.dirty = true;
      return *it->second.fb;
  }

  void TileSurfaceCache::copy_on_write(TileId id) {
      auto it = m_surfaces.find(id);
      auto clone = std::make_unique<Framebuffer>(m_tile_w, m_tile_h);
      clone->copy_pixels_from(*it->second.fb);  // memcpy tile-sized
      it->second.ref_count--;
      m_surfaces[id] = TileSurface{
          .fb = std::move(clone),
          .id = id,
          .dirty = true,
          .ref_count = 1
      };
  }
  ```

  Expected impact: Zero framebuffer_acquire_ms for tile surfaces (preallocated),
                    zero framebuffer_clear_ms for non-dirty tiles, L2 cache stays hot.
  Complexity: Medium (1-2 weeks)


