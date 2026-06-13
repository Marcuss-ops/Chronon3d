================================================================================
  PRIORITY ORDER RECOMMENDATION
================================================================================

  TODAY (1-2 days):
    1. N4  — any_cast → enum dispatch              (20 min)
    2. N10 — RAII guard thread_local ptrs           (15 min)
    3. S3  — L1/L2 prefetch in hot loops            (few hours)
    4. I4  — Thread-to-core affinity                (few hours)
    5. N2  — Box blur 3-pass parallelization        (1 day)

  THIS WEEK (3-5 days):
    6. N1  — Motion blur parallel + SIMD            (2 days)
    7. M3  — SPSC lock-free queue for encoder       (2 days)
    8. N5  — Scene fingerprint → XXH3               (1 day)
    9. N7  — Shadow/glow fused draw                 (1 day)
   10. M5  — Transform cache                        (2 days)

  THIS MONTH (2-4 weeks):
   11. L8V1 — Parallel tile rendering (TBB-first)   (2-3 weeks)
   12. M1  — Frame graph compiler / display list     (2-3 weeks)
   13. P3  — Tile surface cache (zero-alloc)         (1-2 weeks)
   14. N3  — Downsample SSAA parallel                (1-2 days)
   15. S9  — ImageCache sharding                     (1 day)

  NEXT MONTH (4-8 weeks):
   16. L1  — GPU backend Vulkan compute              (4-8 weeks)
   17. L2  — ECS architecture                        (4-8 weeks)
   18. L3  — RDG resource barriers                   (3-4 weeks)
   19. M29 — Test coverage missing nodes             (3-5 days)

  FUTURE (2-6 months):
   20. L4  — Persistent daemon mode                  (2-3 weeks)
   21. L6  — HarfBuzz text shaping                   (2-3 weeks)
   22. L7  — MSDF font atlas                         (2-3 weeks)
   23. L5  — Distributed render farm                 (1-2 months)
   24. L9  — CI multi-platform                       (1-2 days)
   25. M28 — Layout solver extended                  (2-3 weeks)


================================================================================
  KEY ARCHITECTURAL PRINCIPLES
================================================================================

  1. MEASURE BEFORE OPTIMIZING
     Every optimization must be validated against baseline telemetry.
     The framebuffer pipeline counters (acquire_ms, clear_ms, enqueue_ms,
     pool_miss_count_*) are your friends. Use them.

  2. TILE-FIRST FROM THE START
     V3 architecture is tile-first, not node-first. Every new feature should
     think in tiles: "which tiles does this affect?" not "which pixels?"

  3. COMPILE ONCE, EXECUTE MANY
     The Frame Graph Compiler principle: move work from runtime to compile
     time. Display lists, baked transforms, precomputed fingerprints — all
     follow this pattern.

  4. ZERO-COPY BY DEFAULT
     Every memory copy in the hot path must have a documented reason.
     Framebuffer::swap_contents, OwnedFB adoption, AVFrame wrapping —
     all eliminate copies.

  5. PARALLELISM AT EVERY GRANULARITY
     Scene-level (render farm), frame-level (speculative), node-level (TBB),
     tile-level (tile scheduler), pixel-level (SIMD). Each level adds.

  6. PERSISTENCE OVER RE-ALLOCATION
     Tile surfaces, baked transforms, disk cache — if it can live longer,
     make it live longer. Zero-allocation runtime is the goal.

  7. BACKWARD COMPATIBILITY
     Every change should keep the old API working. Use adapters
     (LegacyNodeAdapter), default parameters, and fallback paths.
     No breaking changes to the public CLI or API.

================================================================================
