================================================================================
  TIER 2 — QUICK WINS PROFESSIONALI
================================================================================

  4. BOX BLUR 3-PASS PARALLELIZATION
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    In effect_blur.cpp, the 3-pass box filter (horizontal → vertical →
    horizontal) is completely serial. Each row/column is processed one at a
    time. For large radii (50+) at 4K, this takes 50-100ms.

  Solution:
    Parallelize each pass with tbb::parallel_for on rows/columns. Merge 3
    passes into 2 by using a larger kernel (2-pass separable).

  Files to modify:
    - src/backends/software/utils/effects/effect_blur.cpp

  Implementation:
  ```cpp
  // Current (serial):
  for (int pass = 0; pass < 3; ++pass) {
      for (i32 y = y0; y < y1; ++y) {  // ← serial rows
          for (i32 x = x0; x < x1; ++x) {
              // horizontal blur
          }
      }
      for (i32 x = x0; x < x1; ++x) {  // ← serial columns
          for (i32 y = y0; y < y1; ++y) {
              // vertical blur
          }
      }
  }

  // Fixed (parallel):
  for (int pass = 0; pass < 2; ++pass) {  // 2-pass separable
      // Horizontal pass — parallel per row
      tbb::parallel_for(tbb::blocked_range<i32>(y0, y1), [&](auto& r) {
          for (i32 y = r.begin(); y < r.end(); ++y) {
              blur_row_horizontal(fb, y, radius, tmp_buffer);
          }
      });

      // Vertical pass — parallel per column
      tbb::parallel_for(tbb::blocked_range<i32>(x0, x1), [&](auto& r) {
          for (i32 x = r.begin(); x < r.end(); ++x) {
              blur_column_vertical(fb, x, radius, tmp_buffer);
          }
      });
  }
  ```

  Expected impact: 4-8× speedup on large-radius blur. 1 day of work.
  Complexity: Low

  ──────────────────────────────────────────────────────────────────────────────

  5. MOTION BLUR ACCUMULATION PARALLEL + SIMD
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    In render_pipeline_composition.cpp, N motion blur samples are evaluated
    and rendered sequentially, then accumulated pixel-by-pixel with
    get_pixel()/set_pixel() overhead.

  Solution:
    (a) Parallelize the N samples with tbb::parallel_for.
    (b) SIMD-ize the accumulation with Highway (4-lane float × 4 channels).
    (c) Reuse framebuffer from pool instead of creating one per sample.

  Files to modify:
    - src/render_graph/render_pipeline_composition.cpp
    - src/backends/software/highway_kernels.cpp (add accum function)

  Implementation:
  ```cpp
  // Each sample renders into its own tile framebuffer from the pool
  struct SampleJob {
      int sample_idx;
      float time_offset;
      OwnedFB framebuffer;
  };

  // Parallel sample rendering
  std::vector<SampleJob> samples(N);
  tbb::parallel_for(tbb::blocked_range<int>(0, N), [&](auto& r) {
      for (int s = r.begin(); s < r.end(); ++s) {
          samples[s].framebuffer = pool.acquire_owned(w, h);
          Scene sub = comp.evaluate(frame, t + samples[s].time_offset);
          render_scene_to_fb(*samples[s].framebuffer, sub, ...);
      }
  });

  // SIMD accumulation
  for (int y = 0; y < h; ++y) {
      Color* __restrict__ dst = output.pixels_row(y);
      for (int x = 0; x < w; x += 4) {
          // HWY SIMD: accumulate 4 pixels × 4 channels = 16 floats
          auto sum = Zero(df);
          for (int s = 0; s < N; ++s) {
              auto px = LoadU(df, &samples[s].framebuffer->pixel(x, y));
              sum = Add(sum, Mul(px, weight));
          }
          StoreU(sum, df, &dst[x]);
      }
  }
  ```

  Expected impact: 4-8× speedup with 8 motion blur samples. 2 days.
  Complexity: Medium

  ──────────────────────────────────────────────────────────────────────────────

  6. SPSC LOCK-FREE QUEUE FOR ENCODER PIPELINE
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    The render thread synchronizes with the writer thread via mutex +
    condition_variable. Each frame enqueue/dequeue causes a context switch
    (syscall). Under load, the render thread blocks waiting for the encoder.

  Solution:
    Single-Producer Single-Consumer lock-free queue. Zero mutex, zero syscalls
    in the hot path. The render thread never waits for the encoder.

  Files to create:
    - include/chronon3d/core/spsc_queue.hpp

  Files to modify:
    - apps/chronon3d_cli/utils/video/video_export_pipe.cpp

  Implementation:
  ```cpp
  // include/chronon3d/core/spsc_queue.hpp
  template<typename T>
  class SpscQueue {
      static constexpr size_t CACHE_LINE = 64;

      // Producer-owned
      alignas(CACHE_LINE) std::atomic<size_t> m_head{0};

      // Consumer-owned
      alignas(CACHE_LINE) std::atomic<size_t> m_tail{0};

      alignas(CACHE_LINE) T m_buffer[CAPACITY];

  public:
      bool push(T&& item) {
          size_t head = m_head.load(std::memory_order_relaxed);
          size_t tail = m_tail.load(std::memory_order_acquire);
          if (full(head, tail)) return false;  // backpressure

          m_buffer[head & MASK] = std::move(item);
          m_head.store(head + 1, std::memory_order_release);
          return true;
      }

      bool pop(T& item) {
          size_t tail = m_tail.load(std::memory_order_relaxed);
          size_t head = m_head.load(std::memory_order_acquire);
          if (tail == head) return false;  // empty

          item = std::move(m_buffer[tail & MASK]);
          m_tail.store(tail + 1, std::memory_order_release);
          return true;
      }

  private:
      static constexpr size_t CAPACITY = 64;  // must be power of 2
      static constexpr size_t MASK = CAPACITY - 1;

      bool full(size_t head, size_t tail) const {
          return (head - tail) >= CAPACITY;
      }
  };
  ```

  Integration:
  ```cpp
  // In video_export_pipe.cpp
  class FfmpegPipeEncoder : public IVideoEncoder {
      SpscQueue<EncodeFrame> m_queue;
      std::thread m_writer_thread;
      std::atomic<bool> m_running{true};

      void writer_loop() {
          EncodeFrame frame;
          while (m_running) {
              if (m_queue.pop(frame)) {
                  write_to_pipe(frame);
              } else {
                  _mm_pause();  // spin-wait, no syscall
              }
          }
      }

      bool encode_frame(const Framebuffer& fb, Frame frame) override {
          // Non-blocking push — never waits
          while (!m_queue.push(EncodeFrame{fb, frame})) {
              _mm_pause();  // backpressure: encoder is behind
          }
          return true;
      }
  };
  ```

  Expected impact: Eliminates synchronization latency. Render thread never waits
                    for the encoder. 2 days.
  Complexity: Medium

  ──────────────────────────────────────────────────────────────────────────────

  7. L1/L2 PREFETCH IN HOT LOOPS
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    The compositing, blur, and color conversion loops access pixel data
    linearly, but at 4K resolution the working set exceeds L2 cache (256KB per
    core for Zen3). The CPU stalls waiting for memory fetches.

  Solution:
    Add _mm_prefetch() intrinsics every 16 pixels (one cache line) to load
    data into L1 before it's needed.

  Files to modify:
    - src/backends/software/highway_kernels.cpp
        Inside composite_normal_premul and similar hot loops
    - src/backends/software/utils/effects/effect_blur.cpp
        Inside box blur horizontal/vertical loops
    - src/video/direct_yuv_converter_hwy.cpp
        Inside YUV conversion loops

  Implementation pattern:
  ```cpp
  // Within each pixel loop:
  for (int x = x0; x < x1; ++x) {
      // Prefetch next cache line (64 bytes = 16 pixels ahead)
      if ((x & 15) == 0) {  // every 16 pixels
          _mm_prefetch((const char*)&src_row[x + 16], _MM_HINT_T0);
          _mm_prefetch((const char*)&dst_row[x + 16], _MM_HINT_T0);
      }

      // Normal pixel work...
      dst_row[x] = composite_pixel(src_row[x], dst_row[x], mode);
  }
  ```

  Expected impact: 3-5% IPC improvement on Zen3. A few hours.
  Complexity: Very Low

  ──────────────────────────────────────────────────────────────────────────────

  8. THREAD-TO-CORE AFFINITY + NUMA
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    TBB threads (in tbb::parallel_for inside graph_executor.cpp) are free to
    migrate between cores. This destroys L1/L2 cache warmth and causes
    cross-socket penalties on multi-socket systems (Threadripper, Xeon).

  Solution:
    Pin each TBB worker thread to a specific physical core via
    pthread_setaffinity_np (Linux) or SetThreadAffinityMask (Windows).

  Files to modify:
    - include/chronon3d/render_graph/graph_executor.hpp
    - src/render_graph/executor/executor.cpp (constructor)

  Implementation:
  ```cpp
  // In GraphExecutor constructor
  GraphExecutor::GraphExecutor()
      : m_arena(std::max(1u, std::thread::hardware_concurrency()))
  {
      // NOTE: affinity must be set BEFORE TBB creates threads, not after.
  // Use tbb::global_control or a custom thread initializer.
  // See oneapi-src/oneTBB/examples for proper affinity setup.

  // Pin calling thread to core 0 (main thread)
      pin_thread_to_core(0);

      // For TBB workers, set affinity via a task_arena initializer
      // or by using partitioner with per-thread initialization
      tbb::global_control gc(
          tbb::global_control::max_allowed_parallelism,
          std::thread::hardware_concurrency()
      );
      
      // Alternative: use tbb::task_arena::attach with a pre-configured
      // thread pool. Or use tbb::info::core_types() to get physical cores.
      m_arena.execute([&] {
          // Only works if TBB hasn't migrated the thread yet
          int core_id = tbb::this_task_arena::current_thread_index();
          pin_thread_to_core(core_id);
      });
  }

  // Helper (Linux)
  void pin_thread_to_core(int core_id) {
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(core_id, &cpuset);
      pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  }
  ```

  Expected impact: 5-10% on multi-socket systems. A few hours.
  Complexity: Very Low

  ──────────────────────────────────────────────────────────────────────────────

  9. any_cast CHAIN → ENUM DISPATCH
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    In effect_stack.cpp, each effect does a chain of std::any_cast to find its
    parameters: try BlurParams, then TintParams, then BrightnessParams, etc.
    This is O(n) type-erasure checks per effect per frame.

  Solution:
    Add an EffectType enum to EffectInstance. Dispatch with switch (O(1))
    instead of any_cast chain (O(n)).

  Files to modify:
    - include/chronon3d/effects/effect_instance.hpp
    - src/backends/software/utils/effects/effect_stack.cpp

  Implementation:
  ```cpp
  // include/chronon3d/effects/effect_instance.hpp
  enum class EffectType : uint8_t {
      Blur, Tint, Brightness, Contrast, Glow,
      DropShadow, Bloom, Fake3DWave, MAX_KINDS
  };

  struct EffectInstance {
      EffectType type;       // <-- NEW, set at construction time
      std::any params;       // still stores the actual params struct

      template<typename T>
      static EffectType type_for() {
          if constexpr (std::is_same_v<T, BlurParams>) return EffectType::Blur;
          if constexpr (std::is_same_v<T, TintParams>) return EffectType::Tint;
          // ... etc ...
      }
  };

  // In effect_stack.cpp — replace any_cast chain:
  void apply_effect(Framebuffer& fb, const EffectInstance& inst) {
      switch (inst.type) {
          case EffectType::Blur: {
              const auto& p = std::any_cast<const BlurParams&>(inst.params);
              apply_blur(fb, p.radius, p.passes);
              break;
          }
          case EffectType::Tint: {
              const auto& p = std::any_cast<const TintParams&>(inst.params);
              apply_tint(fb, p.color, p.intensity);
              break;
          }
          // ... etc ...
      }
  }
  ```

  Expected impact: Small per-effect, but accumulates over layers with 4-5 effects each.
                    20 minutes.
  Complexity: Very Low

  ──────────────────────────────────────────────────────────────────────────────

  10. RAII GUARD FOR THREAD-LOCAL PTRS
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    profiling::g_current_trace, g_current_frame, g_current_counters, and
    g_current_framebuffer_pool are set and reset manually in
    software_renderer.cpp. If an exception is thrown, the pointers stay dirty,
    causing dangling pointer dereferences in subsequent frames.

  Solution:
    RAII guard that sets thread_local pointers on construction and resets them
    on destruction (including stack unwinding from exceptions).

  Files to modify:
    - include/chronon3d/core/profiling.hpp
    - src/backends/software/software_renderer.cpp
    - src/render_graph/render_pipeline_composition.cpp

  Implementation:
  ```cpp
  // include/chronon3d/core/profiling.hpp
  class ProfilingGuard {
  public:
      ProfilingGuard(RenderTrace* trace, int32_t frame,
                     RenderCounters* counters, FramebufferPool* pool) {
          // Save previous values
          m_prev_trace    = g_current_trace;
          m_prev_frame    = g_current_frame;
          m_prev_counters = g_current_counters;
          m_prev_pool     = g_current_framebuffer_pool;

          // Set new values
          g_current_trace    = trace;
          g_current_frame    = frame;
          g_current_counters = counters;
          g_current_framebuffer_pool = pool;
      }

      ~ProfilingGuard() {
          // Restore previous values (even if called during stack unwinding)
          g_current_trace    = m_prev_trace;
          g_current_frame    = m_prev_frame;
          g_current_counters = m_prev_counters;
          g_current_framebuffer_pool = m_prev_pool;
      }

      // Non-copyable, non-movable
      ProfilingGuard(const ProfilingGuard&) = delete;
      ProfilingGuard& operator=(const ProfilingGuard&) = delete;

  private:
      RenderTrace* m_prev_trace;
      int32_t m_prev_frame;
      RenderCounters* m_prev_counters;
      FramebufferPool* m_prev_pool;
  };

  // Usage in software_renderer.cpp — replaces manual assignments:
  std::shared_ptr<Framebuffer> SoftwareRenderer::render_frame(...) {
      ProfilingGuard guard(&m_trace, static_cast<int32_t>(frame),
                          &m_counters, m_framebuffer_pool.get());

      // ... rest of function ...
      // No manual g_current_* assignment needed
      // If exception thrown, guard destructor restores previous values
  }
  ```

  Expected impact: Robustness — no dangling pointers post-exception. 15 minutes.
  Complexity: Very Low

  ──────────────────────────────────────────────────────────────────────────────

  11. TEMPORAL HASHING — CACHE NODE WITH HISTORY
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    A node is recomputed even if its inputs haven't changed compared to previous
    frames. For linear animations (camera motion, opacity fade), each frame
    produces a slightly different hash, invalidating the cache even though the
    visual difference is negligible.

  Solution:
    Hash the node (params + transform + time) + hash of the last N frames.
    If the sequence is identical → reuse interpolated result. Also cache
    the N most recent frames' hashes to detect stable sequences.

  Files to create/use:
    - include/chronon3d/cache/temporal_cache.hpp (new)
    - src/cache/temporal_cache.cpp

  Implementation:
  ```cpp
  // include/chronon3d/cache/temporal_cache.hpp
  struct TemporalCacheKey {
      u64 node_hash;            // hash of node parameters + type
      u64 input_hash;           // hash of inputs for this frame
      u64 history_hash[4];      // hash of 4 previous frames

      u64 digest() const {
          return XXH3_64bits(this, sizeof(*this));
      }
  };

  class TemporalCache {
      static constexpr int HISTORY_DEPTH = 4;

      // Ring buffer of recent hashes per node
      std::unordered_map<u64, std::array<u64, HISTORY_DEPTH>> m_history;

      u64 update_history(u64 node_id, u64 current_hash) {
          auto& hist = m_history[node_id];
          // Shift history: [h3, h2, h1, h0] → [h2, h1, h0, current]
          for (int i = 0; i < HISTORY_DEPTH - 1; ++i)
              hist[i] = hist[i + 1];
          hist[HISTORY_DEPTH - 1] = current_hash;
          return compute_stability(hist);
      }

      // Returns 1.0 if all 4 frames identical, 0.0 if all different
      float compute_stability(const std::array<u64, HISTORY_DEPTH>& hist) {
          bool all_same = true;
          for (int i = 1; i < HISTORY_DEPTH; ++i)
              if (hist[i] != hist[0]) all_same = false;
          return all_same ? 1.0f : 0.0f;
      }
  };
  ```

  Expected impact: For linear animations with stable parameters: skip complete
                    node render. 3-5 days.
  Complexity: Medium

  ──────────────────────────────────────────────────────────────────────────────

  12. IMAGECACHE SHARDING + READ-WRITE LOCK
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    ImageCache uses a single std::mutex for all operations. During multi-threaded
    image preload, all threads contend on the same lock.

  Solution:
    Transform into sharded cache with std::shared_mutex (16 shards). Reads use
    shared_lock (parallel), writes use unique_lock (exclusive).

  Files to modify:
    - include/chronon3d/backends/assets/image_cache.hpp
    - src/backends/assets/image_cache.cpp

  Implementation:
  ```cpp
  class ImageCache {
      static constexpr int SHARD_COUNT = 16;

      struct Shard {
          std::shared_mutex mutex;
          std::unordered_map<std::string, std::shared_ptr<CachedImage>> map;
      };

      std::array<Shard, SHARD_COUNT> m_shards;

      Shard& shard_for(const std::string& key) {
          std::hash<std::string> hasher;
          return m_shards[hasher(key) & (SHARD_COUNT - 1)];
      }

      std::shared_ptr<CachedImage> get_or_load(const std::string& path) {
          auto& shard = shard_for(path);

          // Read: shared lock (parallel with other reads)
          {
              std::shared_lock lock(shard.mutex);
              auto it = shard.map.find(path);
              if (it != shard.map.end()) {
                  return it->second;
              }
          }

          // Write: unique lock (exclusive)
          std::unique_lock lock(shard.mutex);
          auto image = load_image_from_disk(path);
          shard.map[path] = image;
          return image;
      }
  };
  ```

  Expected impact: Parallel image preload 2-3× faster. 1 day.
  Complexity: Low

  ──────────────────────────────────────────────────────────────────────────────

  13. DOWNSAMPLE SSAA PARALLEL + RAW POINTER ACCESS
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    downsample_fb() in render_pipeline_helpers.hpp calls src.get_pixel(ix, iy)
    and dst->set_pixel(x, y, ...) for every pixel — function call + bounds check
    overhead. Loop is entirely serial.

  Solution:
    (a) Direct row pointer access instead of get_pixel/set_pixel.
    (b) Parallelize with TBB.
    (c) SIMD-ize the box filter with Highway.

  Files to modify:
    - src/render_graph/render_pipeline_helpers.hpp

  Implementation:
  ```cpp
  void downsample_fb_simd(const Framebuffer& src, Framebuffer& dst) {
      const int dst_w = dst.width();
      const int dst_h = dst.height();
      const int factor = src.width() / dst_w;

      tbb::parallel_for(tbb::blocked_range<int>(0, dst_h), [&](auto& r) {
          for (int y = r.begin(); y < r.end(); ++y) {
              const Color* __restrict__ src_rows[4]; // up to 4x SSAA
              Color* __restrict__ dst_row = dst.pixels_row(y);

              for (int dy = 0; dy < factor; ++dy) {
                  src_rows[dy] = src.pixels_row(y * factor + dy);
              }

              for (int x = 0; x < dst_w; ++x) {
                  int sx = x * factor;
                  // Accumulate factor×factor box filter with raw pointers
                  float r = 0, g = 0, b = 0, a = 0;
                  for (int dy = 0; dy < factor; ++dy) {
                      for (int dx = 0; dx < factor; ++dx) {
                          const auto& c = src_rows[dy][sx + dx];
                          r += c.r; g += c.g; b += c.b; a += c.a;
                      }
                  }
                  float inv = 1.0f / (factor * factor);
                  dst_row[x] = Color{r * inv, g * inv, b * inv, a * inv};
              }
          }
      });
  }
  ```

  Expected impact: 2-4× speedup with SSAA 2× active. 1-2 days.
  Complexity: Low

  ──────────────────────────────────────────────────────────────────────────────

  14. SHADOW/GLOW MULTI-LAYER FUSED DRAW
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    draw_shadow() and draw_glow() in effect_stack.cpp draw 5-6 separate layers
    with progressive spread. Each calls draw_transformed_shape() separately —
    redundant draw calls.

  Solution:
    Fuse into a single pass with cumulative alpha accumulation in a temporary
    buffer instead of 6 separate draws.

  Files to modify:
    - src/backends/software/utils/effects/effect_stack.cpp

  Implementation:
  ```cpp
  void draw_shadow_fused(Framebuffer& fb, const Shape& shape,
                         const Matrix3x2& model, Color base_color,
                         float spread, RasterizeState* state)
  {
      static constexpr int LAYERS = 6;

      // Pre-compute alpha for all layers
      float alphas[LAYERS];
      float total_alpha = 0;
      for (int i = 0; i < LAYERS; ++i) {
          alphas[i] = base_color.a / (i + 1);
          total_alpha += alphas[i];
      }
      // Normalize
      for (int i = 0; i < LAYERS; ++i) alphas[i] /= total_alpha;

      // Single draw with cumulative alpha
      Color blended_color = base_color;
      blended_color.a = total_alpha * base_color.a;
      draw_transformed_shape(fb, shape, model,
                           {blended_color.r, blended_color.g, blended_color.b,
                            blended_color.a},
                           spread, state);
  }
  ```

  Expected impact: ~5-6× fewer draw calls per node with shadow/glow. 1 day.
  Complexity: Low

  ──────────────────────────────────────────────────────────────────────────────

  15. PERSISTENT DISK CACHE PER TILE
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    The existing DiskNodeCache (disk_node_cache.hpp) caches whole nodes, not
    tiles. A static background tile that was rendered in a previous session is
    lost. Next session re-renders it.

  Solution:
    Extend DiskNodeCache with tile-level caching. Each tile gets a key
    combining node_hash + tile_id. Static tiles survive across sessions.

  Files to modify:
    - src/cache/disk_node_cache.cpp

  Implementation:
  ```cpp
  // DiskNodeCache — add tile-level methods
  class DiskNodeCache {
      struct TileCacheKey {
          u64 node_digest;      // hash of node parameters
          int tile_x, tile_y;   // tile coordinates
          int width, height;    // tile dimensions

          std::string filename() const {
              return fmt::format("{:016x}_{}_{}_{}_{}.tile",
                  node_digest, tile_x, tile_y, width, height);
          }
      };

  public:
      bool get_tile(const TileCacheKey& key, Framebuffer& output);
      void put_tile(const TileCacheKey& key, const Framebuffer& tile);
      bool tile_exists(const TileCacheKey& key) const;

      // Bulk operations for preloading visible tiles
      void preload_visible_tiles(const TileMask& visible_mask,
                                 const std::vector<TileCacheKey>& keys);
  };
  ```

  Expected impact: Static tiles survive across sessions → zero re-render time
                    for static backgrounds. 2-3 days.
  Complexity: Medium

  ──────────────────────────────────────────────────────────────────────────────

  16. SCENE FINGERPRINT HASH → XXH3
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    compute_scene_fingerprint() in render_pipeline_helpers.hpp manually hashes
    ~50+ values per layer using XOR + 0x9e3779b97f4a7c15 constant. For 100
    layers: thousands of hash operations per frame with potential collisions.

  Solution:
    Replace manual combine with XXH3_64bits_update() (already a project
    dependency). Use incremental hash — only hash what changed. Cache the
    hash when the scene is static.

  Files to modify:
    - src/render_graph/render_pipeline_helpers.hpp

  Implementation:
  ```cpp
  // Before:
  uint64_t compute_scene_fingerprint(const Scene& scene) {
      uint64_t h = 0;
      for (auto& layer : scene.layers()) {
          h ^= std::hash<float>{}(layer.opacity()) + 0x9e3779b97f4a7c15;
          // ... manual XOR/combine for each field ...
      }
      return h;
  }

  // After (using XXH3):
  uint64_t compute_scene_fingerprint(const Scene& scene) {
      XXH3_state_t state;
      XXH3_64bits_reset(&state);

      for (auto& layer : scene.layers()) {
          XXH3_64bits_update(&state, &layer.opacity(), sizeof(float));
          XXH3_64bits_update(&state, &layer.transform(), sizeof(Transform));
          XXH3_64bits_update(&state, &layer.blend_mode(), sizeof(BlendMode));
          XXH3_64bits_update(&state, layer.effects_hash());  // pre-computed
          // ... XXH3_update for each relevant field ...
      }

      return XXH3_64bits_digest(&state);
  }

  // Cache when scene is static:
  struct FingerprintCache {
      std::optional<uint64_t> cached;
      uint64_t scene_version;  // increment when scene structure changes

      uint64_t get_or_compute(const Scene& scene) {
          auto current_version = scene.version();
          if (cached.has_value() && scene_version == current_version) {
              return *cached;
          }
          cached = compute_scene_fingerprint(scene);
          scene_version = current_version;
          return *cached;
      }
  };
  ```

  Expected impact: 2-3× faster fingerprint, more robust hash with fewer collisions.
                    1 day.
  Complexity: Low

  ──────────────────────────────────────────────────────────────────────────────

  17. ENCODER ZERO-COPY FRAME DELIVERY
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    frame_conversion_copy_ms shows the time spent copying frame data before
    sending to the encoder. The rendered RGBA framebuffer is copied to a
    separate YUV buffer before avcodec_send_frame().

  Solution:
    Eliminate intermediate copies: create an AVFrame that directly wraps the
    framebuffer's pixel data (or a tile's pixel data). Use av_frame_get_buffer()
    with pre-registered buffers to write directly into encoder memory.

  Files to modify:
    - apps/chronon3d_cli/utils/video/native_av_encoder.cpp
    - include/chronon3d/video/direct_yuv_converter.hpp

  Implementation:
  ⚠️ IMPORTANT NOTE: libx264 expects YUV420P input, not RGBA.
     Using libx264rgb (different codec, different compression) would accept
     RGBA but produces larger files. The true zero-copy approach requires
     one of:
       (a) Convert to YUV in the encoder thread (async, not zero-copy but
           removes conversion from render thread hot path)
       (b) Write an AVFrame that wraps YUV buffer from the converter pool
           (av_frame_get_buffer + av_image_fill_arrays with pool memory)
     The approach below wraps RGBA for contexts where libx264rgb is acceptable.

  ```cpp
  // Create an AVFrame that references the framebuffer's pixel data directly
  // (zero copy for the RGBA→encoder path, requires libx264rgb codec)
  AVFrame* av_frame_from_framebuffer(const Framebuffer& fb) {
      AVFrame* frame = av_frame_alloc();
      frame->format = AV_PIX_FMT_RGBA;  // requires libx264rgb codec
      frame->width  = fb.width();
      frame->height = fb.height();
      frame->linesize[0] = fb.width() * 4;  // RGBA = 4 bytes/pixel
      frame->data[0] = const_cast<uint8_t*>(
          reinterpret_cast<const uint8_t*>(fb.pixels())
      );
      return frame;
  }

  // In native_av_encoder.cpp:
  bool NativeAvEncoder::write_frame(const Framebuffer& fb, ...) {
      if (m_use_direct && m_codec_allows_rgba) {
          AVFrame* direct_frame = av_frame_from_framebuffer(fb);
          int ret = avcodec_send_frame(m_codec_ctx, direct_frame);
          av_frame_free(&direct_frame);
          return ret >= 0;
      } else {
          // Convert to YUV in encoder thread (existing path)
          return write_frame_yuv(fb, ...);
      }
  }
  ```

  Expected impact: -30-50% of frame_conversion_copy_ms. 2 days.
  Complexity: Medium

  ──────────────────────────────────────────────────────────────────────────────

  18. POOL ADAPTIVE PREALLOCATION FROM TELEMETRY
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    The FramebufferPool has fixed preallocation. If a scene needs more
    framebuffers than preallocated, the pool misses and falls back to heap
    allocation. Telemetry counters (framebuffer_pool_miss_count_empty,
    framebuffer_acquire_ms) already exist but are not used for self-tuning.

  Solution:
    At the start of each frame, read the telemetry counters and adapt the pool:
    if miss_count > threshold, increase pool size. If acquire_ms > 5ms,
    preallocate more.

  Files to modify:
    - src/cache/framebuffer_pool.cpp
    - include/chronon3d/cache/framebuffer_pool.hpp

  Implementation:
  ```cpp
  // In FramebufferPool:
  class FramebufferPool {
      // ... existing members ...

      // Adaptive preallocation
      void adapt_from_telemetry(const RenderCounters& counters) {
          // If pool misses due to empty buckets, increase size
          if (counters.framebuffer_pool_miss_count_empty > 3) {
              const float increase_factor = 1.5f;
              for (auto& bucket : m_buckets) {
                  bucket.capacity = static_cast<int>(bucket.capacity * increase_factor);
              }
              // Trigger preallocation at next acquire
              m_needs_prealloc = true;
          }

          // If acquire time is high, preallocate more aggressively
          if (counters.framebuffer_acquire_ms > 5.0) {
              for (auto& bucket : m_buckets) {
                  bucket.warm_count = std::min(
                      bucket.capacity,
                      bucket.warm_count + 2  // add 2 more per bucket
                  );
              }
          }

          // Hysteresis: don't oscillate
          m_last_adapt_frame = current_frame;
      }

  private:
      int m_last_adapt_frame{0};
      static constexpr int ADAPT_COOLDOWN_FRAMES = 30;  // don't adapt more than once per 30 frames
      bool m_needs_prealloc{false};
  };
  ```

  Expected impact: Self-tuning pool — no more pool misses in production. 1 day.
  Complexity: Low

  ──────────────────────────────────────────────────────────────────────────────

  19. TRANSFORM CACHE — BAKED ANIMATION RESULTS
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    TransformNode recomputes the transform matrix every frame, even for
    identical animations (e.g. linear opacity fade, position animation between
    keyframes). The animation keyframe list is static per composition.

  Solution:
    Cache the resolved transform per frame. If the keyframe sequence is
    unchanged, reuse the previously computed matrix.

  Files to modify:
    - src/scene/layer.cpp (Layer::evaluate_transform)
    - include/chronon3d/scene/layer.hpp

  Implementation:
  ```cpp
  // In Layer:
  class Layer {
      struct TransformCache {
          std::unordered_map<Frame, Matrix3x2> baked_transforms;
          uint64_t keyframe_version{0};  // invalidated when keyframes change
          Matrix3x2 last_identity;
      };

      mutable TransformCache m_transform_cache;

      Matrix3x2 evaluate_transform(Frame frame) const {
          // Check cache
          auto it = m_transform_cache.baked_transforms.find(frame);
          if (it != m_transform_cache.baked_transforms.end()) {
              return it->second;
          }

          // Compute (expensive)
          Matrix3x2 result = m_anim_transform.evaluate(frame);

      // Cache for next time (bounded ring buffer, last 1024 frames)
      if (m_transform_cache.baked_transforms.size() > 1024) {
          // Evict oldest when buffer is full (render > 1024 frames)
          auto oldest = m_transform_cache.baked_transforms.begin();
          m_transform_cache.baked_transforms.erase(oldest);
      }
      m_transform_cache.baked_transforms[frame] = result;
          return result;
      }

      void invalidate_transform_cache() {
          m_transform_cache.baked_transforms.clear();
          m_transform_cache.keyframe_version++;
      }
  };
  ```

  Expected impact: For scenes with many animated layers: ~5-10% speedup. 2 days.
  Complexity: Medium


