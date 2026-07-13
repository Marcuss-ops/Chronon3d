# TICKET-TEXT-REVEAL-MODULE-UNIT-TESTS-DEFERRED - Chaser-chore for multi-agent race recovery

## Status
OPEN. Orchestrator chose ACCEPT + chaser-chore at 3rd push failure per AGENTS.md b589fdba 3-attempt recovery session precedent.

## Honest-limitation disclosure
The original chore 0dc0933fe8b0d309e0bec171820414bbfa3497d7 test(text): add shape_glyph_line 8-TEST_CASE module-unit-tests was raced-out by a persistent multi-agent race window. Per AGENTS.md Post-push SHA-selfcheck invariant: orphan commit + tree + blobs are preserved in git's unreachable objects until git gc runs. Local backup at /tmp/lost-chore-shape_glyph_unit_tests/.

## Forward-points
1. WBH session recovery: cherry-pick orphan 0dc0933f onto clean main when race window has cooled.
2. macchina-verifica: WBH ctest -R test_glyph_layout --output-on-failure expects 8/8 PASS.
3. Cat-5 3-doc closure: doc-syncing docs/CURRENT_STATUS.md cite-only row once macchina-verifica PASS.

## VPS env-block
VPS lacks working build env per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV - macchina-verifica deferred to working build host.

## Cross-references
- AGENTS.md Post-push SHA-selfcheck invariant
- AGENTS.md b589fdba 3-attempt recovery session precedent
- AGENTS.md 21ece2b3 unique-edit recovery variant
- Orphan SHA: 0dc0933fe8b0d309e0bec171820414bbfa3497d7
- Local backup: /tmp/lost-chore-shape_glyph_unit_tests/
- Subject envelope: test(text): add shape_glyph_line 8-TEST_CASE module-unit-tests (56 chars)
