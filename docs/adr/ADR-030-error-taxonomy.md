# ADR-030 — Canonical error taxonomy

**Status:** Accepted — Phase 1 implementation
**Date:** 2026-09-11

## Decision

Chronon has one engine-level error taxonomy in
`include/chronon3d/core/types/error.hpp`. `chronon::ErrorCategory` is limited
to:

`Recoverable`, `FrameFailed`, `JobFailed`, `Cancelled`, `DeviceLost`,
`InvalidPlan`, and `InvariantViolation`.

`chronon::Error` carries one `ErrorDetail` payload. The payload preserves the
origin domain, legacy enum type/code, message, component, node, asset, frame,
and job identity. Domain enums remain source-compatible during the strangler
migration; they are diagnostic detail and must not become alternative control
taxonomies.

## Layer contract

The responsibility chain is fixed and is enforced by the architecture gate:

1. A backend reports an operation failure with its domain error.
2. The executor decides whether the current frame can continue.
3. `JobSession` performs the terminal `Failed` transition and retains the
   first structured error.
4. The API boundary returns that structured error to the caller.

Production code may not silently swallow `catch (...)` and continue. A
fallback or downgrade must be resolved before execution and carry both a
structured reason and a counter/receipt.

## Compatibility

`core/error.hpp` remains a forwarding include for source compatibility. Legacy
error enums are mapped through `from_legacy<Enum>()`; they are not renamed in
one batch. The duplicate internal render diagnostic enum is now named
`RenderDiagnosticCode`, leaving the SDK `RenderErrorCode` as the public legacy
adapter.

## Consequences

Callers can make control-flow decisions from one category while retaining
domain-specific diagnostics. Removal of a legacy enum is permitted only when
its demolition-debt card reaches the zero-direct-construction exit condition
and its equivalence test is green.
