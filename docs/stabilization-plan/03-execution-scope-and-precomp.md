# ExecutionScope e Precomp

> **Fonte canonica del dettaglio:** [`docs/03-execution-scope-and-precomp.md`](../03-execution-scope-and-precomp.md) — contratto formale per l'isolamento degli scope di esecuzione, gerarchia `root → tile / precomp`, regole di catena parent + arena, protezione anti-ricorsione.

## Stato sintetico

| Componente | Stato |
|---|---|
| `ExecutionScope` tipo + enum | 🟢 Done (PR 6.0) |
| `would_recurse` + `is_descendant_of` | 🟢 Done (PR 6.0) |
| `kMaxScopeDepth` clamp + `would_overflow()` | 🟢 Done (PR 6.5) |
| Test core scope (12 TEST_CASE) | 🟢 Done (PR 6.0 + 6.5 + 6.6) |
| `GraphExecutor::execute_with_scope` overload | 🟢 Done (PR 6.1) |
| Plumbing Root → Tile | 🟢 Done (PR 6.4) |
| Precomp child scope + ProgramLease | 🟢 Done (PR 6.3) |
| Migrazione call site produttivi | 🟢 Done (PR 6.1-6.4) |
| Eliminazione `local_session` fallback tile | 🟢 Done (PR 6.4 — zero istanze residue) |
| Test memory, race, recursion, lease | 🟢 Done (PR 6.6/6.7) |

## Ancora aperto

- [ ] WP-7: Ritirare l'overload legacy `execute()` (attualmente `[[deprecated]]`).
- [ ] WP-7: Ritirare il child ctor senza arena esplicita (attualmente `[[deprecated]]`).
- [ ] Migrare i call site del test lattice rimasti al nuovo overload per rimuovere i warning di deprecazione. (NON bloccante — i percorsi produttivi sono migrati.)

## Completato quando

Tutti i call site produttivi usano `ExecutionScope` ✅; root, tile e precomp condividono solo servizi intenzionalmente condivisi ✅; nessun child invalida il parent ✅; test memory, race, recursion e lease sono verdi ✅.
