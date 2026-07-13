# TICKET-CLI-ISOLATE-RUNTIME-DEV — CLI 3-layer split (production + content bridge + DEV-gated)

| Field | Value |
|---|---|
| ID | TICKET-CLI-ISOLATE-RUNTIME-DEV |
| Priorità | P2 |
| Area | apps/chronon3d_cli (CLI runtime isolation) |
| Stato | NEW (planning; cronaca pending) |
| Wrapper | `apps/chronon3d_cli/` (3-layer split planned) |

## Scopo sintetico

CLI 3-layer split:

1. **Production layer** — shipped CLI surface: `chronon3d render`, `chronon3d inspect`, `chronon3d validate`, etc. (no dev-only paths).
2. **Content bridge layer** — cross-language ABI surface for the `.chronon` format + consumer integration (the SDK boundary).
3. **DEV-gated layer** — experimental + diagnostic commands (e.g., `chronon3d bench`, `chronon3d dev-probe`) gated behind `CHRONON3D_ENABLE_DEV` build flag (the pattern already canonical for the `software-renderer` boundary per `docs/ARCHITECTURE_AUDIT.md`).

The split establishes a canonical CLI runtime surface (production + bridge) with DEV-gated paths isolated from the public ABI. Goal: align CLI surface with SDK ABI contract so consumer apps do not accidentally import DEV paths.

## Stato

PLANNING. Cronaca: not yet started. The stub exists to honor the I5 user directive that established the canonical cronaca home for the row in `docs/FOLLOWUP_TICKETS.md`.

## Forward-points

- (to be populated on first work commit) — likely sub-areas: production surface audit, content bridge ABI alignment, DEV-gate machinery, consumer-test scaffolding.

## Cross-references

- `docs/FOLLOWUP_TICKETS.md` — open-blocker row (this ticket's index entry).
- `docs/ARCHITECTURE_AUDIT.md` §III Software-Renderer Boundary (the canonical precedent for the `*_ENABLE_DEV` build-flag gating pattern).
- AGENTS.md §regole "No nuovi singleton/registry/resolver/cache senza ADR" (any new CLI surface / runtime split requires an ADR before implementation).
- `docs/CHANGELOG.md` (Cite-Only entry on first work commit, per AGENTS.md §Disciplina di aggiornamento dei canonici).
