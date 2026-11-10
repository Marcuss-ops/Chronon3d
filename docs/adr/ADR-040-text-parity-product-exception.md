# ADR-040 — Text AE parity product exception

| Field | Value |
|---|---|
| **Status** | Accepted — 2026-11-10 (product decision; not a parity pass) |
| **Date** | 2026-11-10 |
| **Deciders** | Product owner (explicit authorization), Chronon3D text/runtime |
| **Tags** | `text`, `parity`, `antialiasing`, `shadow`, `stroke`, `exception` |
| **Related** | [text closure][text-closure], text animator parity ticket, `TICKET-TEXT-QUALITY-V2`, `ADR-038`, `ADR-039` |

## Context

The [text closure ticket][text-closure] was reopened for full text parity. Its exit
contract required all eight frozen pixel cases to pass unchanged thresholds,
strict native Vulkan execution, byte-identical software repeats, and native
implementations of GPU canonical shadow (`T1-01`), GPU vector stroke (`T1-02`)
and the transition/span/paint-range combination (`T2-01`). The ticket explicitly
allowed a changed acceptance only through an explicit product ADR.

The 2026-11-10 post-revert build on CLI SHA
`ba6e14ad788fcab97000affb177f0f7bbb367595b3ffe52dd579c6990a1da75e` passes
**1/8** cases (`multipage`). The other seven miss at least one frozen threshold:
`latin_basic` IoU 0.985900, `transitions` 0.983597, `fractional` 0.963981,
`exact_3d` 0.943576, `rtl_caret` 0.928066, `multi_shadow` 0.927669 and
`effects` 0.923020. A second 2x2 coverage probe was measured and rejected:
`latin_basic` IoU fell to 0.962797 and `transitions` to 0.956730, so the shader
was reverted. The GPU canonical shadow, GPU vector stroke and T2-01 paths are
not implemented; they remain explicit fail-closed rejections.

The product owner has now explicitly authorized the AA exception and the
closure of the text AE parity cluster. This ADR records that decision. It does
not lower a threshold and does not declare the seven residual cases a pass.

## Decision

1. Accept the measured AA/coverage, effect-composition and 3D residual for the
   text AE parity closeout. The frozen thresholds remain unchanged as the
   technical target for future work.
2. Close the [text closure ticket][text-closure] as `CLOSED — product exception`, with
   `1/8` PASS and the seven failing cases documented. The technical matrix
   keeps their `OPEN`/`PARTIAL` states; the ticket status is a product
   acceptance, not a parity PASS.
3. Defer `T1-01` GPU canonical shadow, `T1-02` GPU `VectorStroke` and `T2-01`
   transitions with spans/paint ranges. The existing fail-closed behavior
   remains; no silent fallback, new renderer, resolver or cache authority is
   introduced. `T2-01` remains the `ADR-039` non-goal.
4. Close the parent text animator parity ticket and the quality
   authority `TICKET-TEXT-QUALITY-V2` under the same exception. Their
   unimplemented GPU/editorial items remain documented debt, not PASS.
5. Keep separate scopes separate: SVG text, GEL surface and DirectYUV/NV12 are
   not closed by this ADR. Their own gates remain authoritative.

## Consequences

### Positive

- The text AE parity cluster can leave the active blocker index without
  pretending that the frozen corpus is green.
- The remaining gap is recorded with per-case metrics, an exact CLI SHA and
  explicit exit conditions.
- The fail-closed GPU paths stay visible and are not replaced by a misleading
  fallback.

### Negative / accepted debt

- Production text still differs from the software reference on seven corpus
  cases, including AA/coverage on curves and diagonals, glow/background and
  multi-shadow composition, and 3D projection.
- GPU canonical shadow and GPU vector stroke remain unavailable; plans that
  request them still fail closed.
- `text_transitions` combined with styled spans remains rejected.
- No full-parity claim can be made for this SHA.

### Neutral

- No runtime, schema, threshold, corpus or cache change.
- The closeout is documentation and evidence only.

## Alternatives considered

- **Leave the ticket open indefinitely.** Rejected: the product owner requested
  closure and the remaining work is a separate funded effort.
- **Lower the thresholds or change the metric.** Rejected: that would hide the
  measured gap and would not be a parity result. The frozen thresholds remain.
- **Implement GPU canonical shadow, vector stroke and T2-01 in this change.**
  Deferred: that is a larger implementation with new GPU passes, atlas keys and
  typed transition payloads, not an AA acceptance decision.
- **Reuse the earlier AA exception silently.** Rejected: the reopened ticket
  required an explicit product ADR. This is that ADR.

## Demolition Debt / exit conditions

The exception is removed only when all of the following are implemented and
validated on the frozen corpus:

1. GPU canonical union-mask shadow with separable blur and linear source-over.
2. GPU `VectorStroke` with join/cap/miter-limit semantics and a dedicated cache
   key.
3. A typed `text_transitions` + spans/paint-ranges path with per-keyframe range
   validation.

Owner: Chronon3D text/runtime. The failure evidence and per-case metrics are in
the [text parity evidence report][text-evidence]. Until those gates
land, the fail-closed paths must remain and no threshold may be changed without
a new ADR.

## References

- [text closure ticket][text-closure] — closeout §17.
- [text parity evidence report][text-evidence] — build, metrics and rejected
  probe.
- `docs/adr/ADR-038-text-quality-shadow-and-stroke-modes.md` — shadow/stroke
  modes and fail-closed policy.
- `docs/adr/ADR-039-text-transition-residuals.md` — T2-01 non-goal.
- `docs/CURRENT_STATUS.md`, `docs/FOLLOWUP_TICKETS.md` — active-index update.

[text-closure]: docs/tickets/TICKET-TEXT-CLOSURE-20261109.md
[text-evidence]: docs/evidence/text-parity-20261110-product-exception/REPORT.md
