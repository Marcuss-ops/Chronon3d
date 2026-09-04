# P2 Cache and CLI cleanup closure

Status: IMPLEMENTED / repository-audited on 2026-09-04.

This note records the authority boundaries used to close P2.2–P2.5. It is deliberately narrow: it does not introduce a registry, cache primitive, CLI enum, or compatibility facade.

## P2.2 — exactly three cache families

The canonical taxonomy is `include/chronon3d/cache/cache_taxonomy.hpp` and contains exactly three families:

1. `ContentCache`
2. `ResidencyCache`
3. `ProgramCache`

Rules:

- Generic in-memory key/value cache storage and eviction delegate to `cache::LruCache`.
- `FramebufferPool` and similar residency managers are specialized `ResidencyCache` machinery, not a fourth generic cache engine.
- `CompiledArtifactCache` is a persistent ProgramCache I/O adapter. It stores compiled artifacts on disk and does not define an in-memory eviction primitive.
- `PreparedAssetDigestCache` is a ContentCache member and now delegates its entry storage to `LruCache`; its outer mutex remains only the existing single-flight/metadata-validation boundary.
- No `FastCache` is introduced.

Census covered the canonical runtime/cache headers plus post-taxonomy cache additions. The post-taxonomy custom digest map was the concrete second-primitive violation found during the census and was migrated to `LruCache`.

## P2.3 — hot-path cache telemetry

`LruCache::Stats` is the canonical source for:

- `hash_time_ns`
- `lock_time_ns`
- `lru_mutation_time_ns`
- `miss_loader_time_ns`
- `contention_count`

The propagation chain is now:

`LruCache::Stats -> make_generic_cache_stats() -> GenericCacheStats -> CacheSnapshot/DomainSnapshot -> format_cache_snapshot()`

The single `make_generic_cache_stats()` conversion is intentional: cache facades must not maintain per-type copies that can silently drop new counters.

The historically diagnostics-registered Lru facades (`NodeCache`, `FrameCache`, `VideoFrameCache`, `ConvertedFrameCache`, `SceneProgramCache`) use that conversion path. `SceneProgramCache` diagnostics reads the underlying canonical `LruCache` stats instead of its reduced tuning facade counters.

Regression coverage in `tests/cache/test_cache_diagnostics.cpp` verifies both single-cache pass-through and per-domain aggregation of all five metrics.

### Measurement semantics

The current telemetry measures cache-operation cost categories, not CPU instruction-level attribution. `hash_time_ns` includes the hash/shard/hash-table lookup work instrumented by `LruCache`; `lru_mutation_time_ns` covers the canonical cache-state/LRU mutation sections. Refining those labels into narrower micro-events is a future measurement-quality improvement, not a second authority.

## P2.4 — parse once, typed runtime where a canonical type exists

CLI parsing helpers in `apps/chronon3d_cli/utils/common/cli_mappers.hpp` convert user-facing representations to canonical runtime types at the CLI boundary, including:

- backend -> `graph::BackendPreference`
- motion blur mode -> `MotionBlurMode`
- temporal sample pattern -> `TemporalSamplePattern`
- temporal filter -> `TemporalFilter`

Runtime-facing CLI state in `render_args.hpp` and command declarations carries those canonical types, including `FramebufferPoolClearPolicy` and `GpuHotPathMode`.

Strings that remain strings (`log_level`, trace strings and similar values) remain stringly because their canonical runtime carriers are strings. P2.4 does not create duplicate CLI-only enums merely to make the CLI look typed; the canonical runtime carrier must change first if those values later become enums.

Definition of done for P2.4: a value that has a canonical runtime enum/type is parsed once at the CLI boundary and passed downstream as that type. No downstream renderer/backend reinterpretation is the authority for those values.

## P2.5 — narrow CLI headers and split implementation units

`apps/chronon3d_cli/commands.hpp` is now a small compatibility umbrella only. New code is directed to the narrow headers:

- `benchmark_args.hpp`
- `diagnostic_args.hpp`
- `render_args.hpp`
- `command_declarations.hpp`

Command and daemon implementation work is compiled from split units rather than accumulating new declarations and implementation in the umbrella. Concurrent daemon cleanup on `main` removed migration/fallback fragments; this closure does not duplicate those headers or reintroduce `.inc` authority.

Demolition debt: remove the compatibility umbrella once its remaining import census reaches zero. Until then it must remain forwarding-only and must not regain command state or implementation.

## Closure invariants

- Exactly three canonical cache families; no fourth family.
- Exactly one generic in-memory cache primitive: `cache::LruCache`.
- No `FastCache`.
- Cache telemetry has one stats authority and one type-erasure conversion path.
- Canonically typed CLI values are parsed once and stay typed downstream.
- `commands.hpp` is compatibility-only; narrow headers / split units are the active architecture.

## Verification boundary

The repository-level audit, diffs, regression test source, and post-write `main` history were verified during this closure. Runtime/build certification still depends on the project build/CI environment; absence of an observed CI result must not be represented as a green build.