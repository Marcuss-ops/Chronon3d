# TICKET-IMAGE-MANIFEST-CATRIDGE-WAVE-02 — Catridge Wave 02: text-shape manifest-clean alignment follow-up

## Stato

OPEN

## Priorità

P1

## Problema

The TICKET-LAYER-IMAGE-MANIFEST-CLEAN lineage (forward-points 0e + 0f+ + 0g+ + 0h+, all closed) established the manifest-clean alignment pattern for `ImageParams::asset_path` (BUCKET-A for scene-builder primitives). Forward-point 0i+ (now living in [`docs/FOLLOWUP_TICKETS.md` §Cartography Architecture](../FOLLOWUP_TICKETS.md#cartography-architecture-catalogued-forward-points)) hooks a re-evaluation gate for the next wave — but the next-wave primitive category is NOT yet specified. WAVE-02 scopes the next wave to **text-shape primitives** (the 10+ structural types in `include/chronon3d/text/` + `content/text/`) which have analogous ambiguities (e.g. `TextSpec::position` was already routed through ADR-019, but `TextRunSpec::origin`, `TextAnimatorSpec::target_frame`, `TextMaterial::font_path` carry overlapping or asset-path-like semantics that have NOT been catalogued).

The current state has NO forward-point reopens-trigger for text-shape primitives — the machine-verification grep in `## Cartography Architecture` is scoped to `include/chronon3d/scene/builders/builder_params.hpp` only; a future PR adding a `std::string` field to e.g. `TextMaterial` (a BUCKET-B-analogue text-shape) would NOT trigger any re-evaluation gate, silently regressing the partition invariant.

## Evidenza

- Machine-verified inventory (2026-07-11, via `grep -lrE 'std::string|std::filesystem::path' include/chronon3d/text/`): 10+ files in `include/chronon3d/text/` carry `std::string` / `std::filesystem::path` fields — `text_document.hpp`, `text_span.hpp`, `text_unit_map.hpp`, `text_document_builder.hpp`, `glyph_selector.hpp`, `timed_text_document.hpp`, `paragraph_style.hpp`, `font_engine.hpp`, `animation/text_pre_shaping.hpp`, `animation/text_animator_stack.hpp` + others, NOT yet subject to the 3-bucket partition invariant.
- §Cartography Architecture currently enumerates **only scene-builder primitives** (10 BUCKET-A/B/C tuples for RectParams + CircleParams + RoundedRectParams + ... + DarkGridBgParams). The next-wave text-shape inventory has no formal partition classification.
- Forward-point 0i+ trigger (in §Cartography `### Catalogued forward-points`) is `new std::string / std::filesystem::path field on a BUCKET-B primitive` — but BUCKET-B is currently the scene-builder BUCKET-B (7 of 10 primitives). Text-shape primitives need their own BUCKET-A/B/C partition before the same trigger can be re-applied.

## Impatto

- **Stabilità**: SDK V1 text-export contract (`chronon3d_cli text-export` consuming text-shape partitions) currently relies on TextSpec / TextDefinition canonical contracts (ADR-019). Without a WAVE-02 partition, future field-adds to text-shape primitives (e.g. `TextMaterial::font_path` becoming a real asset-path field) would silently expand the public SDK surface without Cat-3 scrutiny, potentially re-introducing ambiguity similar to pre-forward-point 0e `ImageParams::path`.
- **Manutenibilità**: §Cartography Architecture's machine-verification command (`grep -nE 'std::string|std::filesystem::path' include/chronon3d/scene/builders/builder_params.hpp`) only covers the scene-builder surface. Adding a second grep target (`include/chronon3d/text/`) + a 1-line `### Catalogued forward-points` entry for WAVE-02 0a would extend the §Cartography invariant to the text-shape surface, restoring alignment-coverage.
- **Release readiness**: SDK V1 release gate ([`docs/RELEASE_GATE.md`](../RELEASE_GATE.md)) does not currently require text-shape manifest-clean alignment. WAVE-02 + its forward-points allow incremental Cat-3-justified progress toward an SDK V2 text-export cleaner contract without gratuitous expansion (Cat-3 anti-duplication respected).

## Confine

This ticket owns:
- The §Cartography Architecture extension to text-shape primitives (machine-verification grep extension + 1-row `### Catalogued forward-points` entry for `TICKET-IMAGE-MANIFEST-CATRIDGE-WAVE-02 0a`).
- The inventory of text-shape primitives partitioned into 3 buckets (BUCKET-A: asset-path-bearing [e.g. font_path]; BUCKET-B: ambiguous-intent-naming only [e.g. Vec3 pos analogue in TextSpec]; BUCKET-C: already-clean).
- Future forward-points (`0b+`) that implement the manifest-clean alignment for ONE BUCKET-A primitive in WAVE-02 (if/when one emerges with a genuine asset-path-like field).

This ticket does NOT own:
- New SDK symbols in `include/chronon3d/text/` (Cat-3 anti-duplication; ADR-gated first).
- Removal of any existing deprecation banners (separate ticket per `## Regole di lint documentale` future-point pattern).
- Text V1 / Camera V1 cert alignment (independent milestones per [`docs/ROADMAP.md`](../ROADMAP.md)).
- Migration of consumers (148+ call sites of legacy APIs already documented in M1.8 §2D `TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS`).

## Soluzione accettabile

CLOSED when ALL of the following are true (per AGENTS.md v0.1 §regole + Cat-3 + Cat-5 + Cat-1):

1. `docs/FOLLOWUP_TICKETS.md` `## Cartography Architecture` section is extended to cover text-shape primitives:
   - `### 3-bucket partition invariant` includes a sub-block on text-shape bucket allocation (BUCKET-A / BUCKET-B / BUCKET-C per the machine-verified inventory, with explicit enumeration of what gets assigned to each bucket).
   - `### Catalogued forward-points` table contains a `TICKET-IMAGE-MANIFEST-CATRIDGE-WAVE-02 0a` row documenting the seed-first-step in the same format as the existing `TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0i+` row.
   - `**Cross-link**:` block extends the machine-verification command to include `include/chronon3d/text/` (alongside the existing `include/chronon3d/scene/builders/builder_params.hpp`).
2. `docs/CHANGELOG.md` has the new prepended entry explaining the §Cartography extension.
3. `docs/CURRENT_STATUS.md` §Stato per area has the SDK Product V1 row (or a new WAVE-02 row if applicable) extended to mention WAVE-02 spawn.
4. ZERO new symbols in `include/chronon3d/text/` (Cat-3 anti-duplication).
5. ZERO source-code modified outside `docs/` (this is a pure doc-only closure forward-point).
6. All 8 pre-push gates PASS + GATE-MNT-01 PASS post-commit.

CLOSED via single atomic chore commit (or 2 if first-step seed + ticket open are split per AGENTS.md "Fare PR piccole e mirate").

## Criteri di accettazione

- Criterion 1 (observable): `bash -c "grep -nE 'std::string|std::filesystem::path' include/chronon3d/text/"` returns a machine-verifiable hit inventory that anchors the WAVE-02 BUCKET-A/B/C partition in §Cartography.
- Criterion 2 (test/gate): all 8 pre-push gates PASS + GATE-MNT-01 PASS post-commit (subject to AGENTS.md §honesty macchina-verifica deferral if VPS auth-block on `git push`).
- Criterion 3 (no regression): TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-points 0e + 0f+ + 0g+ + 0h+ remain DONE; forward-point 0i+ remains the LIVING scene-builder re-evaluation gate; WAVE-02 0a is the LIVING text-shape re-evaluation gate superset.
- Criterion 4 (doc sync): Cat-5 3-doc same-commit closure (CHANGELOG + FOLLOWUP_TICKETS + CURRENT_STATUS) closes inside ONE-or-TWO atomic chore commits; no retroactive amendments to historical CHANGELOG entries.

## Collegamenti

- ADR:
  - [`docs/adr/ADR-019-text-coordinate-model.md`](../adr/ADR-019-text-coordinate-model.md) (TextSpec position ambiguity already addressed; WAVE-02 extends the analytics to other text-shape primitives).
  - [`docs/adr/ADR-018-auto-fit-text.md`](../adr/ADR-018-auto-fit-text.md) (related but distinct — WAVE-02 is manifest-clean alignment, NOT auto-fit; orthogonal scope).
- baseline: not baseline-gated (this is a doc-only ticket; no source-code state change).
- milestone: SDK V1 release ([`docs/RELEASE_GATE.md`](../RELEASE_GATE.md) §SDK V1) + Text V1 cert ([`docs/ROADMAP.md`](../ROADMAP.md) §M1.8 text shape cluster).
- ticket correlati:
  - [TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0i+](../FOLLOWUP_TICKETS.md#cartography-architecture-catalogued-forward-points) (parent lineage; WAVE-02 is the text-shape analogue of the scene-builder 0i+ trigger).
  - [TICKET-SIMPLICITY-COORDINATES](../FOLLOWUP_TICKETS.md) (DONE — ADR-019 text coordinate model; WAVE-02 builds on this).
  - [TICKET-TEXT-VISIBILITY-PIPELINE](../FOLLOWUP_TICKETS.md) (PLANNED — text visibility audit pipeline; orthogonal to WAVE-02 manifest-clean alignment).
- cross-doc: [`docs/FOLLOWUP_TICKETS.md` §Cartography Architecture](../FOLLOWUP_TICKETS.md#cartography-architecture-catalogued-forward-points) (canonical living home for the WAVE-02 forward-points catalogue).
