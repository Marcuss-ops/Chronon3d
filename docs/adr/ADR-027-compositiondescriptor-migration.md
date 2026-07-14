# ADR-027: CompositionRegistry::add() deprecation discipline + ABI-stability plan

| Field | Value |
|---|---|
| **Status** | Accepted (2026-07-14) |
| **Date** | 2026-07-14 |
| **Deciders** | Chronon3D Composition API maintainers |
| **Tags** | , , , , ,  |
| **Related** | [TICKET-COMPOSITIONDESCRIPTOR-MIGRATION](docs/tickets/TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md) (parent ticket, Phase 1+2+3+4 plan), AGENTS.md section 2x-in-one-chore deprecation-reversal rule (the rule this ADR operationalizes), AGENTS.md Cat-2 freeze (no new SDK API + no source removal without ADR), [ADR-024](ADR-024-deprecate-persistent-framebuffer-cache.md) (sibling deprecation-ADR precedent), [ADR-011](ADR-011-camera-legacy-deletion.md) (sibling class-surface-removal precedent) |
| **Supersedes** | Re-introduces the deprecation discipline that the recovery chore removed |

## Context

CompositionRegistry is the canonical surface for registering composition factories in Chronon3D. The B2 (CompositionDescriptor migration) decided that the canonical registration form is . The recovery chore removed the [[deprecated]] marker on the legacy  overload, abandoning the B2 metadata-deprecation intent.

TICKET-COMPOSITIONDESCRIPTOR-MIGRATION was born to document the abandoned migration intent. Phase 1 consolidated the duplicate  map into  (single source of truth). Phase 2 (this ADR) re-introduces the [[deprecated]] marker with an ABI-stability removal plan.

## Decision

Re-introduce the [[deprecated]] marker on the legacy CompositionRegistry::add(std::string, Factory) overload (Phase 2), plan REMOVAL post V0.1 (Phase 3 + Cat-2 freeze source-removal gate), and bundle the forward-point migration tickets per AGENTS.md section 2x-in-one-chore rule.

### Phase 2 (this chore, 2026-07-14)

Edit : add  on the legacy  overload.

### Phase 3 (forward-point, post V0.1)

Remove the legacy  overload from  (Cat-2 freeze source-removal gate, this ADR serves as the formal gate). All 265 pre-B2 call sites MUST be migrated to the canonical  form FIRST.

### Phase 4 (forward-point, post V0.2)

Audit  returns a non-null  for every registered composition.

### Forward-point tickets (bundled per AGENTS.md section 2x-in-one-chore rule)

- PHASE-2-DEPRECATED-MARKER: DONE 2026-07-14 (this chore)
- PHASE-2.5-BUILD-FLAG-WIRING: OPEN (forward-point)
- PHASE-3-OVERLOAD-REMOVAL: OPEN (forward-point, post V0.1)
- PHASE-4-AUDIT-DESCRIPTOR-OF: OPEN (forward-point, post V0.2)

## Consequences

### Positive

- Restores B2 metadata-deprecation intent that the recovery chore abandoned
- Closes the OPP renderer per-preset logging gap (post-Phase 3)
- Establishes ABI-stability decision boundary (this ADR is the formal gate for Phase 3)
- Bundles forward-points atomically with this chore per AGENTS.md section 2x-in-one-chore rule

### Negative

- Build break on  consumers (the 200+ pre-B2 call sites)
- Cat-3 minimal-surface tradeoff: adding CMake build-flag now would inflate the chore
- Documentation fragmentation: this ADR + ticket + INDEX update are 3 docs to maintain

## Alternatives considered

### A. Keep the legacy overload non-deprecated

Rejected: fails AGENTS.md honesty + the B2 migration was a deliberate architectural decision.

### B. Add the CMake build-flag escape hatch NOW

Rejected for this chore: Phase 2.5 deferred to forward-point per AGENTS.md section Fare PR piccole e mirate.

### C. Use  around the legacy add()

Rejected: anti-pattern; the warning must propagate to the 200+ call sites to drive the migration.

### D. Wait for all 200+ callers to migrate before re-adding the marker

Rejected: too large; defers the deprecation indefinitely.

## macchina-verifica

VPS-only (this session):  = 1 (the new marker). cmake-configure + ctest DEFERRED-WBH per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV.

## References

-  (the file this ADR modifies)
-  (the parent ticket)
- The recovery chore that removed the marker (this ADR is the formal reversal)
- AGENTS.md section 2x-in-one-chore deprecation-reversal rule (operationalized)
- AGENTS.md Cat-2 freeze (this ADR is the formal source-removal gate for Phase 3)
- ADR-024 + ADR-011 (sibling deprecation-ADR precedent pattern)
