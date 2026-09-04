# Runtime queue architecture

This document defines the canonical queue taxonomy for Chronon3D runtime code.
The goal is to keep producer/consumer semantics explicit and to prevent queue
primitive proliferation.

## Canonical primitives

| Primitive | Producer | Consumer | Blocking | Allocation | Canonical use case |
| --- | --- | --- | --- | --- | --- |
| `FrameQueue` | frame pipeline stage | next frame pipeline stage | no (`try_push` / `try_pop`) | TBB-owned bounded queue storage | handoff of `FrameSlotId` only; frame payload remains in `FrameSlotPool` |
| `BoundedChannel<T>` | generic producer thread/stage | generic consumer thread/stage | yes (`push` / `pop`, optional timed pop) | `std::queue` may allocate while growing up to the configured capacity | generic bounded producer/consumer handoff with back-pressure and cancellation |
| `BoundedSpscRing<T, N>` | exactly one producer thread | exactly one consumer thread | no | allocation-free after construction | exceptional SPSC hot path only when a benchmark demonstrates a material win over the canonical alternatives |

Confirmed production example for `BoundedChannel<T>`: `AsyncEncoderSink` uses
`BoundedChannel<AsyncEncodeTask>` between frame submission and its encoder
worker. The queue is deliberately blocking and bounded to provide encoder
back-pressure.

`FrameQueue` is the canonical frame handoff abstraction. It carries slot IDs,
not frame payloads, and therefore must be preferred whenever ownership remains
in `FrameSlotPool`.

## Architecture rule

Runtime code MUST NOT introduce a fourth queue/ring/channel primitive.

New handoff code must select one of the three canonical roles above. A new
primitive is allowed only as a replacement for an existing canonical role and
must include, in the same change:

1. a caller census,
2. benchmark evidence showing why an existing primitive is insufficient,
3. migration/removal of the superseded primitive, and
4. an update to this document.

`BoundedSpscRing` is intentionally conditional rather than a default choice. If
a reliable repository-wide census shows no production callers, delete it. If a
production caller remains, document that caller and the benchmark that justifies
why it does not use `FrameQueue` or `BoundedChannel<T>`.

## Census procedure

Before changing the taxonomy, search all production sources, tests and tools
for:

- `BoundedChannel`
- `BoundedSpscRing`
- `FrameQueue`
- `std::queue` combined with mutex/condition-variable synchronization
- other hand-written ring buffers
- `tbb::concurrent_*queue`

Classify every result by producer, consumer, blocking semantics, allocation
behaviour and use case. `std::queue` used as private storage inside
`BoundedChannel<T>` is implementation detail, not a fourth runtime primitive.

## Current verification note

The GitHub code-search index can report `incomplete_results`; a zero-result from
that index is not sufficient evidence to delete `BoundedSpscRing`. Deletion
requires a reliable repository checkout grep (or equivalent complete tree
scan) followed by the normal build/test gate.
