# Golden fixture review process

Golden fixtures (`tests/**/*.golden`) pin a value that a test compares
against. Changing one is a contract change and must be reviewed as one.

Gate: `python3 tools/check_golden_review.py` (exit 1 on structural problems).

## Required header

Every golden starts with comment lines:

```
# Canonical <what is pinned> for <test or fixture>.
# Regenerate only when the <contract> intentionally changes.
#
# YYYY-MM-DD — <one-line reason>, previously <old value>.
# Evidence: <ticket, test name, or doc path>.
# Review: pending | reviewed by <name> on YYYY-MM-DD
```

## Steps for an intentional change

1. Confirm the new value is caused by the intended change (not by an
   unrelated regression) — run the test and inspect the diff of the
   normalized program / output that the fixture pins.
2. Regenerate the value and add a dated entry with the old value and the
   evidence. The dated entry must be newer than the one in HEAD.
3. Set `# Review:` to `pending` until a second person (or the owner of the
   contract) has inspected the new output; then set `reviewed by ... on ...`.
4. Run the gate and the owning test target; record both exit codes.

A golden that is reproducible but not yet reviewed is reported as
`UNREVIEWED` by the gate. It is not a failure, but it must not be described
as verified correct.

## Current fixtures

| Fixture | Review state |
|---|---|
| `tests/render_graph/compiler/frame_graph_program.golden` | pending — value regenerated 2026-10-08 (0x1ec334584fada888); reproducible, correctness not reviewed |
