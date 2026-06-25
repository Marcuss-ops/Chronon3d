# ExecutionScope e Precomp

> **Fonte canonica del dettaglio:** [`docs/03-execution-scope-and-precomp.md`](../03-execution-scope-and-precomp.md) — contratto formale per l'isolamento degli scope di esecuzione, gerarchia `root → tile / precomp`, regole di catena parent + arena, protezione anti-ricorsione.

## Stato sintetico

| Componente | Stato |
|---|---|
| `ExecutionScope` tipo + enum | 🟢 Done (PR 6.0) |
| `would_recurse` + `is_descendant_of` | 🟢 Done (PR 6.0) |
| `kMaxScopeDepth` clamp + `would_overflow()` | 🟢 Done (PR 6.5) |
| Test core scope (12 TEST_CASE) | 🟢 Done (PR 6.0 + 6.5) |
| `GraphExecutor::execute_with_scope` overload | 🟢 Done (PR 6.1) |
| Plumbing Root → Tile | 🟡 Partial (PR 6.4) |
| Precomp child scope + ProgramLease | 🟢 Done (PR 6.3) |
| Migrazione call site produttivi | 🟡 Partial |
| Eliminazione `local_session` fallback tile | 🟡 Partial |
| Test memory, race, recursion, lease | 🔵 Planned (PR 6.6/6.7) |

## Ancora aperto

- [ ] Migrare tutti i call site produttivi dal vecchio overload a `execute_with_scope`.
- [ ] Migrare i call site dei test al nuovo overload.
- [ ] Eliminare i `RenderSession local_session` rimasti nei fallback tile.
- [ ] Riutilizzare sessione e cache parent in tutti i percorsi tile.
- [ ] Aggiungere test memory lifetime root → tile → precomp (PR 6.6).
- [ ] Aggiungere test race su sibling Precomp (PR 6.6).
- [ ] Aggiungere guard permanenti (PR 6.7).

## Completato quando

Tutti i call site produttivi usano `ExecutionScope`; root, tile e precomp condividono solo servizi intenzionalmente condivisi; nessun child invalida il parent; test memory, race, recursion e lease sono verdi.
