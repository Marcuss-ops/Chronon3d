# TICKET-OPP-BG-CONSUMER — obsolete background-field forward-point

## Stato: CLOSED-SUPERSEDED (2026-07-31)

This ticket described a proposed `CompositionSpec::background_color_rgba`
field and an OPP clear-pass consumer. The field was never consumed by the
renderer and has now been removed from `CompositionSpec` rather than retained
as dead public API.

The current contract is intentionally narrower: `CompositionSpec` contains
only authoring metadata (name, dimensions, frame rate and duration). A future
background-clear feature must introduce a tested, canonical render-setting or
render-session input together with its OPP consumer; it must not reintroduce a
dead field on `CompositionSpec`.

## Verification

- `CompositionSpec::background_color_rgba`: absent from production headers and
  implementation.
- Golden-matrix background cells remain deferred; no false-green coverage is
  claimed.
- No ADR or OPP wiring is required for this closed obsolete ticket.

## Forward-point

If background color becomes a release requirement, open a new focused design
and implementation ticket for the canonical clear-pass input and its visual
uniqueness test. This ticket is not an active blocker.
