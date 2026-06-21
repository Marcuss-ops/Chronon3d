# Chronon3D — Active Roadmap

Current maturity: [`STATUS.md`](STATUS.md). Architecture packages:
[`refactor-roadmap/README.md`](refactor-roadmap/README.md).

## P0 — correctness and validation

| Work | State |
|---|---|
| Fix architecture-boundary script final exit handling | 🔴 Blocked |
| Migrate scheduler tests off `ExecutionPlanCache` and raw graphs | 🔴 Blocked |
| Synchronize `PrecompNode` and borrow the session executor | 🔴 Blocked |
| Move node identity assignment to cloned contexts | 🔴 Blocked |
| Add root/tile/child execution scopes | 🔵 Planned |
| Close install-consumer and global-state SDK boundaries | 🟡 Partial |
| Record clean required CI/build/test evidence | 🔴 Blocked |

P0 must complete before more architecture is added.

## Active feature and performance work

### Partial

- Motion-blur accumulation SIMD verification.
- Three-pass box-blur verification and benchmarks.
- Blend dispatcher specialization where benchmarks justify it.
- Temporary framebuffer aliasing ownership.
- `LayoutFlow` and `LayoutGrid`.
- Zero-copy encoder delivery.
- Pool miss-reason dashboard.
- SPSC queue only after correctness and benchmark proof.
- Dedicated tests for Shadow, Glow, Bloom, Gradient, DoF, and Mask nodes.

### Planned

- Parallel SIMD SSAA downsample.
- Adaptive framebuffer-pool preallocation.
- ISPC blur evaluation against Highway.
- Speculative multi-frame rendering.
- NUMA-aware framebuffer allocation.

## V3 tile-first

All ten pillars in [`V3_BLUEPRINT.md`](V3_BLUEPRINT.md) remain planned.

## Expressions V2

- Quarantine and default exclusion: complete.
- TICKET-003 and TICKET-004: closed historical fixes.
- Stable SDK export/install: not done.
- `AnimatedValue` integration: not done.
- Benchmark, replacement map, retirement deadline: not done.
- Quarantine removal: not done.

The authoritative promotion contract is
[`EXPRESSIONS_V2_PROMOTION.md`](EXPRESSIONS_V2_PROMOTION.md).
