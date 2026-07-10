# TICKET-FOLLOWUP-DE-DUP-REFERENCES — SHA-cite dedup sweep in `docs/adr/*.md`

## Stato

OPEN (closes when macchina-verifica recipe `git grep` over `docs/adr/` produces 0 (file:sha7) pairs with >1 line AND per-ADR DUPLICATE_TARGET atomic commits land on `main`).

## Priorità

P2 (doc-governance follow-up; chains from TICKET-FOLLOWUP-PRECEDENT-DOCS §Regole di lint documentale bucket).

## Problema

L'ADR corpus contiene SHA-cite duplicate: per ogni ADR, coppie `(file_path, sha_7_char_prefix)` con >1 occorrenza testuale violano la regola `AGENTS.md` §`## Regole di lint documentale` §`SHA cite pattern (inline-only rule)` (introdotta in commit `78919613`). Di per sé non è rot funzionale — è un drift di scopribilità + segnale ripetuto in luoghi distinti del singolo ADR — ma l'aggregazione sistematica produce onere di aggiornamento multi-punto per ogni commit-successore.

## Evidenza

Sweep machine-verified al commit di scoperta:

```bash
# Comando utente verbatim (fallito per env issue, vedi sotto)
rg -nE '[0-9a-f]{7,}' docs/adr/*.md
# → grep config error: unknown encoding: [0-9a-f]{7,}
#   (ripgrep 13.0.0, C.UTF-8; probabilmente ~/.ripgrep config conflict)

# Sostituto equivalente via git grep (97 tuple totali, 10 ADR file impattati)
git grep -nE '[0-9a-f]{7,}' -- docs/adr/
```

Risultato del sweep (97 tuple in 10 ADR):

| File | # tuple | # SHA unici con >1 cite | Classificazione |
|---|---|---|---|
| `ADR-001-frame-graph-compiler.md` | 2 | 1 (`9f9af90e` × 2) | DUPLICATE_TARGET |
| `ADR-010-boundary-gate-semantic-extension.md` | 3 | 1 (`6e0c7413` × 3) | DUPLICATE_TARGET (multi-context) |
| `ADR-011-camera-legacy-deletion.md` | 1 | 0 | NOT_A_DEDUP_TARGET |
| `ADR-012-chronon3d-sdk-manifest-boundary.md` | 9 | 3 (`6f7570c9`×3, `a1f5e645`×4, `d73e7e0b`×3) | DUPLICATE_TARGET |
| `ADR-013-camera-v1-semantics.md` | 10 | 1 (`ac514fea`×4) + 6 single-cite in tabella | DUPLICATE_TARGET |
| `ADR-015-screen-space-trs-invariant.md` | 17 | 1 (`c03ce2a2`×17) | **EXEMPT_SPECIAL_CASE** |
| `ADR-016-sequence-asset-canonical-contract.md` | 17 | multi-SHA (`33798b0a`×5, `0f47d591`×7, `fab2046e`×5, `7eb5c2ba`×2) | LEGITIMATE_MULTI_CITE |
| `ADR-017-commit-layer-manifest-preservation.md` | 2 | 1 (`0ff8b100`×2) | DUPLICATE_TARGET |
| `ADR-020-shared-static-fontengine-singleton.md` | 14 | multi (`3b833565`×3, `aae298e7`×2, `2ba38c78`×2, `6f3db3ed`×2, `7eb5c2ba`×5) | DUPLICATE_TARGET (alcuni) + LEGITIMATE (baseline 7eb5c2ba) |
| `docs/adr/INDEX.md` | 3 | 0 (cross-ref ad altri ADR, legitimo) | LEGITIMATE (INDEX cross-reference) |

## Impatto

- **Scopribilità** (`rg` audit): duplicati della stessa SHA nello stesso file producono falsi positivi sul count di "quante referenze" per quella SHA.
- **Onere di manutenzione**: future entries su commit-successori di una SHA deduplicata vanno aggiornate in N punti anziché 1.
- **Conformità AGENTS.md**: la regola testè introdotta (commit `78919613`) enuncia che le cite duplicate sono rot — applicarla al corpus ADR esistente è il passaggio successivo naturale.

## Confine

- Solo file in `docs/adr/*.md`. Le occorrenze in `docs/FOLLOWUP_TICKETS.md`, `docs/CHANGELOG.md`, `docs/baselines/main-<sha>-baseline.md`, commit messages, `tools/*`, `include/chronon3d/**` sono fuori scope (non sono ADR; applicano regole di doc-governance differenti).
- EXEMPT_SPECIAL_CASE per `ADR-015` (la cui intera esistenza è la doc-consolidation del commit `c03ce2a2`) e per `ADR-016` (la cui complessità multi-step richiede multi-SHA-multi-role cite per leggibilità).
- Nessuna modifica al codice (`include/chronon3d/**` intatto). Output: ticket file (questo) + sweep report nel commit body al momento dell'apertura. Per-ADR fix come commit atomici separati.

## Soluzione accettabile

La soluzione completa è articolata in **3 sotto-task**:
1. Per ogni DUPLICATE_TARGET produrre un commit atomico che mantiene la cite col contesto narrativo più ricco e rimuove le cite ridondanti (consolidando inline il info payload); subject convention `docs(adr): ADR-NNN inline-one SHA cite (de-dup COMMIT_SHA)`.
   - For forward-points whose SHA appears in non-trivial wrapping (backticks, em-dashes, smart-quotes), prefer Python byte-equality replace over sed — saves the 3-cycle silent-failure tax demonstrated by ADR-001 / 9f9af90e.
2. Per EXEMPT_SPECIAL_CASE (ADR-015, ADR-016) aggiungere una nota in §References footer del ADR che dichiara la giustificazione dell'exemption + cross-link a questo ticket.
3. Macchina-verifica finale a 0 (file, sha7) pair con >1 occorrenza.

### § Per-ADR dispatch (forward-point, da materializzare come commit atomici separati)

#### ADR-001 / 9f9af90e [DUPLICATE_TARGET]

  **KEEP**: L55 — References bullet `* Commit 9f9af90e: rimozione ExecutionPlanCache.`
  **TRIM**: L33 — body prose `rimozione è registrata nel commit 9f9af90e.`; reformat a `rimozione registrata.` (lo SHA-token verrà rimosso poiché già presente in L55).

#### ADR-010 / 6e0c7413 [DUPLICATE_TARGET (multi-context)]

  **KEEP**: L108 — Branch context `Branch: chore/boundary-gate-semantic-checks @ main@6e0c7413 (fast-forwarded to 6e0c7413 from 14dbc415).`
  **TRIM/REFORMAT**: L21 + L25 — L21 `main@6e0c7413 (this branch's fork point)` + L25 `main@6e0c7413 (RenderPipeline already deleted on the fast-forward)`; consolidare in un'unica nota footer `* Pre-flight target SHA: main@6e0c7413 (fork point + RenderPipeline already deleted)` che assorbe i 2 contesti.

#### ADR-012 / 6f7570c9 [DUPLICATE_TARGET]

  **KEEP**: L120 — References Git log `Git log: 6f7570c9 fix(build): ... Pre-ADR prerequisite for install-side cleanup.`
  **KEEP_REWORDED** (drop SHA prefix; preserve role): L5 — header `Snapshot: main@6f7570c9 — 2026-06-30.` → reword a `Snapshot: 2026-06-30. Linux-only. (commit SHA: see §References — L120)`. Risultato: SHA rimane solo in L120 (1 cite per file post-fix → macchina-verifica 0 satisfied).
  **TRIM**: L77 — body cite `... see fix(build): resolve chronon3d_runtime<->backend_software OBJECT library cycle at commit 6f7570c9`; reformat a testo senza SHA inline, riferimento via L120.

#### ADR-012 / a1f5e645 [DUPLICATE_TARGET]

  **KEEP**: L118 — References Git log `Git log: a1f5e645 fix(sdk): prune chronon3d.hpp umbrella to manifest-only re-export — origin of the residual concern.`
  **KEEP_REWORDED** (drop SHA prefix; preserve role-body): L32 — body cite `... motivated the umbrella prune in commit a1f5e645 (fix(sdk): prune chronon3d.hpp umbrella to manifest-only re-export): an installer-visible re-export violates manifest discipline.` → reword a `... motivated the umbrella prune (fix(sdk): prune chronon3d.hpp umbrella to manifest-only re-export): an installer-visible re-export violates manifest discipline. (commit SHA: see §References — L118)`. Risultato: SHA rimane solo in L118.
  **TRIM**: L95 + L113 — L95 `(P3-I commit a1f5e645)` + L113 `umbrella, pruned to manifest-only re-export in P3-I (a1f5e645)`; reformat a testo senza SHA prefix (riferimento al L118 implicito).

#### ADR-012 / d73e7e0b [DUPLICATE_TARGET]

  **KEEP**: L119 — References Git log `Git log: d73e7e0b fix(sdk): declare missing interface file sets on chronon3d_sdk target — File-set alignment.`
  **KEEP_REWORDED** (drop SHA prefix; preserve prose): L77 — body cite `... and the prior committed file-set fix at d73e7e0b)` → reword a `... and the prior committed file-set fix (commit SHA: see §References — L119)`. Risultato: SHA rimane solo in L119.
  **TRIM**: L111 — `(Post-P3 Step-3 alignment, commit d73e7e0b)`; reformat a `(Post-P3 Step-3 alignment)` (rimozione SHA, già in L119).

#### ADR-013 / ac514fea [DUPLICATE_TARGET]

  **KEEP**: L9 — Decision 5 context `... il pre-existing rot at src/render_graph/pipeline/camera_change_policy.cpp:24 fixata in commit ac514fea ...` (rot-fix lineage).
  **TRIM**: L340 + L385 + L406 — ognuno ripete `... fix in ac514fea ...`; reformat a prose senza SHA prefix (`the rot fixata` invece di `the rot fixata in ac514fea`); SHA resta solo in L9.

#### ADR-017 / 0ff8b100 [DUPLICATE_TARGET]

  **KEEP**: L117 — References `(commit 0ff8b100)`.
  **TRIM**: L5 — header-table row `| ... (landed main@0ff8b100, 2026-07-08) |`; reformat a `| ... (landed 2026-07-08) |` (rimozione SHA, già in L117).

#### ADR-020 / 3b833565 [DUPLICATE_TARGET]

  **KEEP**: L152 — References bullet `3b833565 feat(text): pre-render invariants — fail-loud instead of silent blank` (full role).
  **TRIM**: L60 + L251 — L60 `fail-loud path was added in commit 3b833565 but only for the` + L251 `(the fail-loud throw, commit 3b833565)`; reformat a prose senza SHA prefix (riferimento al L152 implicito).

#### ADR-020 / aae298e7 [DUPLICATE_TARGET]

  **KEEP**: L30 — inline cite `(aae298e7 chore(text): cleanup include + diagnostic format).` (con role in body context).
  **TRIM**: L258 — References bullet `Commit aae298e7 ...`; rimuovere (duplicato di L30).

#### ADR-020 / 2ba38c78 [DUPLICATE_TARGET]

  **KEEP**: L250 — inline cite `... originally introduced in commit 2ba38c78 ... then modified to lazy-mount cwd via commit d4737889 ...` (con multi-role + lineage in un singolo cite).
  **TRIM**: L259 — References bullet `Commit 2ba38c78 ...`; rimuovere.

#### ADR-020 / 6f3db3ed [DUPLICATE_TARGET]

  **KEEP**: L7 — `(commit 6f3db3ed)` Superseded-by header (full role).
  **TRIM**: L260 — References bullet `Commit 6f3db3ed ...`; rimuovere.

### § EXEMPT_SPECIAL_CASE (ADR-015, ADR-016)

#### ADR-015 / c03ce2a2 [EXEMPT_SPECIAL_CASE]

  NO dedup. ADR-015 IS itself the consolidation ADR per `c03ce2a2` (corpus matrix-fix); le 17 cite sono giustificate strutturalmente: ognuna in un contesto narrativo differente (pre-fix rot, decision rationale, acceptance criterion, gate documentation, ecc.).
  Action: aggiungere in §References footer del ADR: `Note: this ADR consolidates 17 cites of commit c03ce2a2 because it IS the documentation-consolidation ADR for that matrix-fix corpus commit. See TICKET-FOLLOWUP-DE-DUP-REFERENCES for the exemption record.`

#### ADR-016 / multi-SHA [LEGITIMATE_MULTI_CITE]

  Multi-SHA cites (`33798b0a` Asset + `0f47d591` Sequence + `fab2046e` Step 2 + `7eb5c2ba` baseline + parallel `a0fbc57b` reconciliation) hanno ruoli semantici distinti nelle sub-sections del ADR (introducer, landed, post-rebase, baseline, parallel-reconciliation, Decision-6.1). NO dedup.
  Action: aggiungere in §References footer del ADR: `Note: this ADR contains multi-SHA cites (33798b0a, 0f47d591, fab2046e, 7eb5c2ba, a0fbc57b) with distinct semantic roles per sub-section. See TICKET-FOLLOWUP-DE-DUP-REFERENCES for LEGITIMATE_MULTI_CITE classification record.`

### § Cross-link TICKET-FOLLOWUP-PRECEDENT-DOCS

Questo ticket chiude parte della closure-criterion "2+ rule aggregate" di TICKET-FOLLOWUP-PRECEDENT-DOCS. Chiusura di quel ticket dopo che questo + n altri rule-related commit sono landed.

## Criteri di accettazione

- **macchina-verifica recipe** (verificata in commit body della sweep di apertura):
  ```bash
  # Per ogni (file_in_docs_adr, sha_7_char_prefix) pair, conta line-occurrence distinct
  rm -f /tmp/adr_sha_all.txt /tmp/adr_sha_duplicates.txt
  for f in docs/adr/*.md; do
    git grep -nE '[0-9a-f]{7,}' -- "$f" 2>/dev/null | while IFS= read -r raw; do
      file=${raw%%:*}; rest=${raw#*:}; line=${rest%%:*}; content=${rest#*:}
      for sha in $(echo "$content" | grep -oE '[0-9a-f]{7,40}'); do
        sha7=$(echo "$sha" | cut -c1-7)
        echo "$file:$line:$sha7"
      done
    done
  done | sort -u > /tmp/adr_sha_all.txt

  awk -F: '{k=$1":"$3; if(k==p){l=l","$2} else{if(NR>1&&l!=""&&c>1) print p":"l; p=k; l=$2; c=1; next} c++}
           END{if(l!=""&&c>1) print p":"l}' /tmp/adr_sha_all.txt | wc -l
  # target: 0 (a regime, dopo che tutti i per-ADR atomic commits sono landed)
  ```
- Tutti i per-ADR DUPLICATE_TARGET sopra elencati hanno un commit atomico dedicato su `main`.
- EXEMPT_SPECIAL_CASE in ADR-015 e ADR-016 documentati esplicitamente con nota footer.
- Nessuna regressione: il payload informativo totale per ogni SHA è conservato (verify con diff prima/dopo manuale + compare `rg`).

## Lineage

- AGENTS.md §"Regole di lint documentale" + §"SHA cite pattern (inline-only rule)" introdotte in commit `78919613` (questo ticket chiude parte della sua "2+ rule aggregate" closure-criterion).
- Commit `4cded60e docs(adr): ADR-020 cleanup — single SHA cite (code review polish)` — esempio precedente di dedup in-linea (rimozione standalone cite duplicata di `d4737889` in ADR-020). Pattern replicato qui su scala corpus-wide.
- Commit `3febd8cd docs(adr): ADR-020 SHA verification + correction if needed` — correzione SHA cite errati, parent commit che mise in luce il pattern.
- Questo ticket nasce dentro il commit di sweep che lo apre.

## Collegamenti

- ADR: nessuno (rules-level ticket, non decisione architetturale).
- baseline: nessuna (pure docs patch, zero compilation footprint).
- ticket correlati: `TICKET-FOLLOWUP-PRECEDENT-DOCS` (parent — questo ticket è parte della closure-criterion di quello), `TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS` (P0 rot pre-esistente scoperto durante lineage, scope differente).
