# TICKET-V3-CLI-UNIFICATION-WATCH-SUPERVISOR — `chronon watch` subprocess supervisor (audit Blocco 4.1)

## Stato: DONE (2026-07-14, commit da assegnare)

## Problema
L'audit statico (sezione §17) identifica che:
- `chronon watch` non esiste come subcommand (`grep` non trova `command_watch`).
- Il daemon (`apps/chronon3d_cli/daemon/daemon_service.cpp:212`) dichiara
  "Restart daemon to pick up new binary" dopo un rebuild — la promessa di
  hot-reload non è mantenuta (il binario in esecuzione non viene ricaricato).
- La `WatchService` menzionata dall'audit non esiste come classe.

## Soluzione adottata (Commit 1 of 3 — Blocco 4.1)
Aggiunto `chronon watch <comp> --frame N -o <output>` come nuovo
subcommand in `apps/chronon3d_cli/commands/watch/register_watch_commands.cpp`.
Algoritmo:

1. Snapshot ricorsivo degli mtime di tutti i file regolari sotto
   `--watch-dirs` (default: `src/`, `include/`, `apps/`).  Skip automatico
   di `build*/`, `.git/`, `vcpkg/`.
2. Sleep `--poll-ms` (default 500 ms), re-snapshot.
3. Se gli mtime differiscono: esegue `--build` (default `bash build-fast.sh`)
   via `std::system`.
4. Se il build ha successo: esegue `std::system` su
   `"<binary>" render <comp> --frame <N> -o "<output>"` per rilanciare
   il rendering nel subprocess (NON nel processo supervisore — il
   supervisore non ricarica mai il proprio codice).
5. Loop infinito; Ctrl+C termina.

Architettura: `std::filesystem::read_symlink("/proc/self/exe")` per il
path del binario (Linux-only; `--chronon-binary` per override).  Niente
.so, niente plugin ABI, niente browser (audit §17 verbatim).

## File toccati
- `apps/chronon3d_cli/commands/watch/register_watch_commands.cpp` (NEW, ~200 LoC)
- `apps/chronon3d_cli/commands.hpp` (WatchArgs struct + command_watch decl)
- `apps/chronon3d_cli/command_registry.hpp` (register_watch_commands decl)
- `apps/chronon3d_cli/command_registry.cpp` (register_watch_commands call)
- `apps/chronon3d_cli/CMakeLists.txt` (file added to chronon3d_cli_core)
- `docs/CHANGELOG.md` (cite-only entry)
- `docs/FOLLOWUP_TICKETS.md` (forward-point row per `--props-file`)

## Forward-points
- **TICKET-WATCH-PROPS-FILE**: il flag `--props-file` è accettato dal
  watch supervisor ma NON viene applicato al subprocess RenderJob
  (manca il per-composition props decoder
  TICKET-ADD-LOADER-FOR-CHRONON-JSON, P1).
- **Commit 2 (Blocco 4.1)**: aggiornare `daemon_service.cpp:212` per
  rimuovere la promessa "Restart daemon" e re-indirizzare gli utenti a
  `chronon watch` (audit §17 "Cosa rimuovere: promessa 'hot reload' del
  daemon").
- **Commit 3 (Blocco 4.1)**: aggiungere `chronon preview <comp> --frames
  0,30,60,90 [--contact-sheet sheet.png]` (audit §18) + rimuovere
  `RenderArgs::quick_frames` (campo morto).

## Criteri di accettazione
- ✅ `chronon watch --help` elenca le opzioni attese (audit §17 verbatim).
- ✅ `chronon3d_cli watch` si registra come subcommand (verified via grep).
- ✅ Il supervisore re-esegue il binario via subprocess (no in-process reload).
- ✅ `docs/CHANGELOG.md` ha la riga cite-only; nessuna cronaca estesa.
- ✅ `docs/FOLLOWUP_TICKETS.md` ha la riga forward-point per `--props-file`.
- ✅ Subject envelope del commit ≤ 72 char.
- ✅ `tools/check_doc_sync.sh` invariato (nessun canonical doc toccato).
- ✅ 5 ZERO-crime rg-probes = 0 (no new add(name,factory), no TextSpec,
  no process-wide asset root, no _FUTURE_TODO, no internal/ SDK leak).
- ⏸  macchina-verifica (build + ctest) DEFERRED-WBH (env-block su questo VPS).
