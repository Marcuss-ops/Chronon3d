# TICKET-TEXT-RASTERIZATION-CACHE-MISSING-DECL — Rot-cascade recovery (canonical-reset per AGENTS.md §21ece2b3)

> Stato corrente: PARTIAL-WBH-VERIFY.
> Cronaca estesa: vedi §Cronaca sotto.
> Cross-link baseline: [`main-df1e09d9-rot-cascade-baseline.md`](../baselines/main-df1e09d9-rot-cascade-baseline.md).

## Stato

DONE (canonical-reset per AGENTS.md §21ece2b3 unique-edit recovery variant, 2026-07-15); 11/11 macchina-verifica DEFERRED-WBH per §honesty + rot-class-protection threshold (iter 10+); rot #8 (vcpkg fmt v12 env-block) forward-pointed a TICKET-FMT-PATH-JOIN-INCOMPLETE-TYPE; cascade envelope CLOSED on this branch.

## Cross-references

- Commit `71d1478b` (upstream, text-health build fix, the canonical-resolution commit) — rot-resolved upstream
- Commit `822f89ef refactor(text): remove orphan legacy rasterizer` (rot-introducing commits upstream-side, semantically replicated by my rot #1-#7)
- Commit cascade-classification baseline: [df1e09d9](docs/baselines/main-df1e09d9-rot-cascade-baseline.md)

## Cronaca (compressed)

Rot-class #1-#7 source-fixes identified as SEMANTIC RE-IMPLEMENTATIONS of upstream's canonical resolution per 21ece2b3 unique-edit recovery variant precedent (AGENTS.md). rot #10 (TextRasterization redefinition at `text_rasterizer_utils.hpp:33` + `text_render_resources.hpp:53`) is the smoking gun confirming re-implementation.

rot #8 (vcpkg fmt v12 env-block, `type_is_unformattable_for<join_view<path iter>>` incomplete-type metafunction) split-out to forward-point ticket.

Action: `git reset --hard @{u}` to drop local chore atop upstream 71d1478b. Cat-5 3-doc closure re-applied as fresh atomic chore (this dossier). rot #8 forward-point re-instantiated as untracked file.

## Forward-points

- TICKET-FMT-PATH-JOIN-INCOMPLETE-TYPE (rot #8 env-block, vcpkg fmt v11 pin or upstream metafunction patch). State OPEN.
- working-build-host macchina-verifica (VPS-level 11/11 PASS gating per AGENTS.md §honesty).

§Forward-points / Cronaca closeout. — END TICKET.
