# TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS — Merge conflict markers committed to tracked files

## Stato

OPEN (pre-existing rot scoperto durante esecuzione di TICKET-FOLLOWUP-PRECEDENT-DOCS)

## Priorità

P0

## Problema

3 file tracciati in `main` contengono merge conflict markers non risolti (`<<<<<<<`, `=======`, `>>>>>>>`) come litterali committati, frutto di cherry-pick/merge incompleti in sessioni precedenti. Sono invalidi sia per `git` (i marcatori sono riservati al sistema di merge) sia per qualunque parser Markdown/Python che li legge come testo.

| File | Tipo |
|---|---|
| `docs/CHANGELOG.md` | Markdown (changelog canonical) |
| `tools/perf/compare_telemetry.py` | Python tooling |
| `tools/perf/pr_gate.py` | Python tooling (CI/perf gate) |

## Evidenza

```bash
# File con marcatori unmerged in HEAD (= origin/main)
git grep -lE '^(<<<<<<<|=======|>>>>>>>)'
# → docs/CHANGELOG.md
# → tools/perf/compare_telemetry.py
# → tools/perf/pr_gate.py

# Conteggio in CHANGELOG.md
git cat-file -p HEAD:docs/CHANGELOG.md | grep -c '<<<<<<<'   # → 1
```

I file sono committed in `HEAD` (= `origin/main`, SHA `4cded60e`); non in working tree (`git status -sb` clean).

## Impatto

- **`docs/CHANGELOG.md`**: il parser Markdown può interpretare i marcatori come testo o come punti di conflict UI, a seconda del renderer. `tools/check_doc_sync.sh` non rileva questo rot-pattern (gate gap). Nessuna nuova entry può essere aggiunta in sicurezza senza prima risolvere i marcatori — appendere testo dopo un fork di marcatori non risolti inserisce contenuto nel branch sbagliato.
- **`tools/perf/*.py`**: i marcatori fanno crashare l'import di Python (`SyntaxError` su `<` e `>` inaspettati). Blocca qualsiasi invocation via CLI o `.github/workflows` che chiama questi script.
- Trust rot: violazione di AGENTS.md §honesty / "non segnare verde una suite che restituisce failure" / "non cambiare un gate per nascondere un errore". Qualunque green-baseline futura che passi senza fixare questo rot è fabricated.

## Confine

Solo i 3 file sopra elencati. Il resto del repo (esclusi i tre) è libero da conflict markers per `git grep -lE '^(<<<<<<<|=======|>>>>>>>)'`.
Non include: marcatori in `docs/ARCHIVE/*` (storici, non operativi), marcatori nei commit messages (stringhe valide, non metadati repo).

## Soluzione accettabile

1. Per ogni file identificato, ispezionare le due versioni `<<<<<<< HEAD` vs `>>>>>>> <branch>` con `git log --oneline -- <file>` + `git show <merge-commit>`.
2. Risolvere manualmente: scegliere la versione corretta (o un merge consapevole di entrambe) per ogni hunk.
3. Una soluzione per file — commit atomico (no mega-commit multi-file risoluzione conflitti).
4. macchina-verifica finale:
   - `git grep -lE '^(<<<<<<<|=======|>>>>>>>)' .` → 0 hit.
   - `python3 -m py_compile tools/perf/compare_telemetry.py tools/perf/pr_gate.py` → exit 0.
5. macchina-verifica Markdown: render di `docs/CHANGELOG.md` pulito senza punti di conflict UI.

## Criteri di accettazione

- Tutti i 3 file ripuliti dai conflict markers.
- `git grep -lE '^(<<<<<<<|=======|>>>>>>>)'` → 0 hit.
- `tools/perf/*.py` importano `SyntaxError`-free.
- `docs/CHANGELOG.md` parsabile Markdown pulito.
- Baseline machine-verified post-fix mantiene 11/11 PASS.
- Entry nel changelog del giorno.

## Lineage

- Scoperto durante preparazione di TICKET-FOLLOWUP-PRECEDENT-DOCS (commit corrente in push).
- Verifica: `git cat-file -p HEAD:docs/CHANGELOG.md | grep -c '<<<<<<<'` = 1 al commit di scoperta (`4cded60e`).
- Questo ticket è documentato ma **non risolto** nel commit corrente per preservare la scope discipline (task corrente = lint-rule promotion; risoluzione conflict-markers è rot separato, atomic single-file commits).

## Collegamenti

- ADR: nessuno.
- baseline: nessuna (rot pre-esistente; baseline precedente `main@7eb5c2ba` GREEN non riproducibile finché i marcatori non sono risolti).
- ticket correlati: TICKET-FOLLOWUP-PRECEDENT-DOCS (ticket aperto dallo stesso commit; scope differente), TICKET-FOLLOWUP-BUILDER-ROT-1..4 (lineage di disclosure rot pre-esistente, scope differente — rots di build, questo ticket è rot di file system).
