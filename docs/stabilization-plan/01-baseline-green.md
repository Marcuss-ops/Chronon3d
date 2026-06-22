# Baseline verde

> **Fonte canonica del dettaglio:** [`docs/01-baseline-green.md`](../01-baseline-green.md) — definizione operativa del baseline verde verificabile, test verdi/rossi, comandi di riproduzione. Vedi anche [`docs/02-determinism.md`](../02-determinism.md) per il contratto determinismo.

## Stato sintetico

| Gate | Stato |
|---|---|
| Build `linux-core-dev` | 🟢 Verde |
| Build `linux-lean` | 🟢 Verde |
| Build `linux-lean-dev` | 🔴 Non verde |
| Test veloci | 🔴 704/707 (3 failure) |
| Gate architetturali | 🔴 2 violazioni |
| Consumer SDK esterno | 🔴 Non verde |

## Blocchi da chiudere in ordine

### 1. Tre test falliti
- [ ] `test_render_session_reset_and_isolation` (percorso concorrente).
- [ ] Test scene che chiama `RenderRuntime::backend()` prima di `attach_backend()`.
- [ ] Secondo test scene con stesso contratto backend non inizializzato.

### 2. Due violazioni architetturali
- [ ] Identificare e correggere le violazioni in `render_session.hpp`.
- [ ] Eseguire `chronon3d_architecture_includes_boundary` con exit code zero.

### 3. Preset `linux-lean-dev`
- [ ] Riprodurre il primo errore da build pulita.
- [ ] Correggere un solo lineage per commit.

### 4. Consumer SDK
- [ ] Correggere propagazione `CMAKE_TOOLCHAIN_FILE` / `CMAKE_PREFIX_PATH`.
- [ ] Installare, configurare ed eseguire consumer esterno.

## Completato quando

- Tutti i target obbligatori restituiscono exit code zero.
- Nessun test fallisce e nessun gate architetturale fallisce.
- Il consumer installato configura, compila ed esegue.
