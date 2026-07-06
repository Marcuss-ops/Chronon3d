# TICKET-GATE-10-PHASE-4-BLACK-FU4 — install_consumer TextRunShape incomplete-type rot → DONE sub-block (B) @ main@0b365354

| Field           | Value |
|-----------------|-------|
| ID              | TICKET-GATE-10-PHASE-4-BLACK-FU4 |
| Priorità        | P0 |
| Area            | install_consumer SDK Phase 4 subgate (cat-1 build rot) |
| Stato           | **DONE sub-block (B)** @ main@0b365354 |
| Lineage         | M1.5#7-AUDIT (forward-decl + sizeof/make_shared rot pattern) |
| Parent ticket   | TICKET-GATE-10-PHASE-4-BLACK (DONE) |
| Anchor commit   | 09e09beb (M1.5#9 PNG save_png top-end off-by-one) |
| Pivot commit    | 0b365354 (FU4-pivot: revert shape.hpp bottom-include + explicit include at main.cpp) |
| Forward ticket  | TICKET-GATE-10-PHASE-4-BLACK-FU5 (PNG content metric rot, NEW) |

## Stato (one-line)
DONE sub-block (B) @ main@0b365354. Sub-block (C) (PNG content metric rot) tracked in [TICKET-GATE-10-PHASE-4-BLACK-FU5](TICKET-GATE-10-PHASE-4-BLACK-FU5.md).

## Soluzione accettabile → Soluzione effettivamente applicata (deviazione postmortem)

### Originariamente proposto
1. Option-preferred: `#include <chronon3d/text/text_run_shape.hpp>` bottom in include/chronon3d/scene/model/shape/shape.hpp.
2. Fallback: add text_run_shape.hpp al SDK public-header manifest.

### Deviazione: Option-preferred causava OPP-internal compile rot
Il bottom-include introdusse una cascade rot nel chronon3d_registry target (4 source files: shape_registry.cpp, animator_resolver.cpp, text_preset_factories_basic.cpp, text_preset_registry.cpp) segnalando `chronon3d::graphics::FillStyle undeclared` + `TextLayoutSpec undeclared`.

### Pivot adottato @ 0b365354
- REVERT della Option-preferred: rimosso bottom-include da shape.hpp.
- ADD di explicit `#include <chronon3d/text/text_run_shape.hpp>` in tests/install_consumer/main.cpp (manifest-clean).
- PRESERVE delle manifest entries (text_run_shape.hpp + text_run_layout.hpp + animated_text_document.hpp) dalla catena 35cb1127/2895bd88/63da8946.

Risultato gate #10 compile + link PASS. Runtime Phase 4 strict FAIL per sub-block (C) PNG content metric rot (FU5).

## Impatto

| Area | BEFORE (c9415e5b) | post-M1.5#9 (09e09beb) | FU4-pivot (0b365354) | post-FU5 (atteso) |
|------|----|----|----|----|
| Gate #10 compile | FAIL PNG + incomplet | FAIL incomplet | **PASS** | **PASS** |
| Gate #10 Phase 4 strict (mean-RGB) | FAIL | FAIL | FAIL | **PASS** (deferred) |
| OPP source compile (chronon3d_registry) | PASS | PASS | **PASS** | **PASS** |
| Feature-freeze V0.1 revocation | blocked | blocked | blocked (FU5) | unblocked |

## Cross-references
- [TICKET-GATE-10-PHASE-4-BLACK](TICKET-GATE-10-PHASE-4-BLACK.md) (parent DONE)
- [TICKET-GATE-10-PHASE-4-BLACK-FU5](TICKET-GATE-10-PHASE-4-BLACK-FU5.md) (sibling OPEN, sub-block C)
- TICKET-M1.5#7-AUDIT (proactive diagnostic for the rot pattern)
- TICKET-GATE-4-LEAK-NEW-FU5 (DONE — abs-path <REPO_ROOT> cleaned to <REPO_ROOT> at line 75)

## Historical notes
Scoperto durante riesecuzione audit 11-gate @ main@c9415e5b. 3-commit chain (35cb1127 + 2895bd88 + 63da8946) introdotta Option-preferred ma rompeva OPP-internal compile. Pivot adoptato per minimizzare blast radius. Documentato deviation postmortem per non-future-repetition.
