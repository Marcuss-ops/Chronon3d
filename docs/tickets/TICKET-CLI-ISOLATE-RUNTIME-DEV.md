# TICKET-CLI-ISOLATE-RUNTIME-DEV — CLI 3-layer split (production + content bridge + DEV-gated)

| Field | Value |
|---|---|
| ID | TICKET-CLI-ISOLATE-RUNTIME-DEV |
| Priorità | P2 |
| Area | apps/chronon3d_cli (CLI runtime isolation) |
| Stato | NEW |
| Wrapper | `apps/chronon3d_cli/` (3-layer split planned) |

## Scopo sintetico

CLI 3-layer split:

1. **Production layer** — shipped CLI surface: `chronon3d render`, `chronon3d inspect`, `chronon3d validate`, etc. (no dev-only paths).
2. **Content bridge layer** — cross-language ABI surface for the `.chronon` format + consumer integration (the SDK boundary).
3. **DEV-gated layer** — experimental + diagnostic commands (e.g., `chronon3d bench`, `chronon3d dev-probe`) gated behind a `*_ENABLE_DEV` build flag (the build-flag gating pattern canonical for diagnostics, see AGENTS.md §regole "Cat-2: Test machinery is a separate scope from production" + the `CHRONON3D_ENABLE_DIAGNOSTICS` precedent).

The split establishes a canonical CLI runtime surface (production + bridge) with DEV-gated paths isolated from the public ABI. Goal: align CLI surface with SDK ABI contract so consumer apps do not accidentally import DEV paths.

## Forward-points

- (to be populated on first work commit) — likely sub-areas: production surface audit, content bridge ABI alignment, DEV-gate machinery, consumer-test scaffolding.
- Future: register `Software-Renderer Boundary` as a numbered section in `docs/ARCHITECTURE_AUDIT.md` (currently no canonical §III — the concept exists in code + AGENTS.md `CHRONON3D_ENABLE_DIAGNOSTICS` precedent but lacks the audit-registry entry). The DEV-gate machinery planned in this ticket would benefit from that audit-registry anchor.

## Cross-references

- `docs/FOLLOWUP_TICKETS.md` — open-blocker row (this ticket's index entry).
- AGENTS.md §regole "Cat-2: Test machinery is a separate scope from production" + the `CHRONON3D_ENABLE_DIAGNOSTICS` build-flag precedent (the canonical `*_ENABLE_*` gating pattern that the DEV layer would follow).
- AGENTS.md §regole "No nuovi singleton/registry/resolver/cache senza ADR" (any new CLI surface / runtime split requires an ADR before implementation).
- `docs/CHANGELOG.md` (Cite-Only entry on first work commit, per AGENTS.md §Disciplina di aggiornamento dei canonici).
