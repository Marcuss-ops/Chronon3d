# TICKET-ADD-LOADER-FOR-CHRONON-JSON — chronon.json loader for `chronon render`

Stato: OPEN (forward-point of TICKET-V3-CLI-UNIFICATION-STARTER-TEMPLATE / Blocco 4.2)

P1 implementation: **LANDED and CLI-fixture E2E verified**. The real generated
standalone template and its host verification remain pending because the
template was removed before it acquired the subprocess argument/output
contract.

## Problema

`Blocco 4.2` (`chronon create <name>`) fornisce un template versionato
(`templates/basic/`) che copia un progetto standalone con un `chronon.json` manifest
(name, version, assets_root, main entry). Il template include un `src/root.cpp` che
compila in un eseguibile standalone il quale renderizza direttamente via
`sdk::RenderEngine`. Tuttavia, il flow aspirazionale della spec utente
`cd my-video && chronon render HelloWorld -o hello.mp4` richiede che il
`chronon render` CLI sappia leggere `chronon.json` di un progetto esterno e
caricare le sue composizioni. L'attuale `chronon render` legge SOLO dalla
`CompositionRegistry` statica baked into il binario CLI (registrazioni in
`register_*_commands` + `register_dev_compositions`).

## Soluzione Confine (PLANNED, non implementata in Blocco 4.2)

Aggiungere un loader per `chronon.json` che:

1. **Discovery**: quando l'utente lancia `chronon render HelloWorld` con CWD
   che contiene `chronon.json`, il CLI:
   - Legge `chronon.json` per ottenere name + assets_root + main entry
   - Localizza il binary compilato (convention: `./build/<name>` o cache)
   - Inietta le composizioni del progetto in una `CompositionRegistry` locale

2. **Composizioni**: due strategie mutuamente esclusive (sceglierne una):

   (A) **Shared library load**: il `chronon render` compila `src/root.cpp` in
       un `.so` e lo `dlopen`-a per estrarre la `CompositionRegistry`. Richiede
       toolchain runtime (cmake + compiler disponibili) — complesso.

   (B) **Subprocess dispatch**: il `chronon render HelloWorld` delega a un
       subprocess `./build/<name> --render HelloWorld -o output.mp4`. Il binary
       standalone già renderizza direttamente; il CLI è un thin wrapper.
       Richiede che l'utente abbia buildato il progetto prima (`cmake -B build
       && cmake --build build`).

   (C) **JSON-only composition spec**: il `chronon.json` dichiara le
       composizioni in formato JSON strutturato (no C++ richiesto). Il CLI
       costruisce la `Composition` runtime leggendo il JSON. Richiede un
       reverse-mapper JSON → Composition (forward-point ulteriore).

3. **Forward-point esplicito**: la spec utente aspirazionale richiede solo che
   il flow "funzioni" dopo Blocco 4.2. La strategia (B) è la più pragmatica per
   V1 (subprocess dispatch, ZERO nuove dipendenze, ZERO nuovo SDK surface).

## Forward-points (chiusura-cycle pending)

- (P1) Implementare strategia (B) — subprocess dispatch — in `command_render`:
  se `chronon.json` esiste in CWD, delega a `./build/<name> --render <comp> -o
  <output>` invece di cercare nella `CompositionRegistry` statica. Richiede che
  l'utente abbia buildato il progetto; il CLI documenta l'errore se il binary
  manca.

- (P2) Strategia (A) — shared library load — per V0.2: richiede toolchain
  detection + fallback graceful.

- (P3) Strategia (C) — JSON-only spec — per V0.3+ : richiede reverse-mapper +
  schema definition.

## Criteri accettazione (per chiusura, P1)

- [x] `chronon render HelloWorld -o hello.mp4` in CWD con `chronon.json` →
      delega a `./build/<name> --render HelloWorld -o hello.mp4` (subprocess).
- [x] Se `./build/<name>` manca → errore user-friendly: "Build the project
      first: `cmake -B build && cmake --build build`".
- [x] ZERO nuove dipendenze (no shared library loader, no in-process compiler).
- [x] ZERO nuovi SDK symbol; ZERO nuovi singleton/registry/resolver/cache
      (eccezione: il dispatcher stesso, già parte di `command_render`).
- [ ] macchina-verifica REAL-GREEN-PASS su TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX-clean host, con il renderer standalone prodotto dal template.

## Cross-link

- `templates/basic/chronon.json` (creato in Commit 1 di Blocco 4.2): il file
  manifest che questo loader dovrà parsare.
- `templates/basic/src/root.cpp` (creato in Commit 1 di Blocco 4.2): il binary
  standalone che il subprocess dispatch invocherà.
- `apps/chronon3d_cli/commands/render/register_render_commands.cpp`: dove
  aggiungere la logica di CWD-detect + subprocess dispatch.
- TICKET-V3-CLI-UNIFICATION-PROFILE-HELP (chiuso 2026-07-14): pattern
  Cat-3 minimal-surface per future aggiunte di CLI ergonomics.
- AGENTS.md §regole "Fare PR piccole e mirate" (P1 + P2 + P3 ciascuno
  atomic-scope).

## Checkout audit — 2026-09-24

- The referenced `templates/basic/chronon.json` and `templates/basic/src/root.cpp`
  are absent from this checkout. Their removal is recorded by
  `537081224 cleanup(cli): delete obsolete project scaffolding`.
- The deleted source is recoverable from Git history, but its `main()` takes no
  arguments: it renders only the hard-coded `HelloWorld` frame to stdout and
  documents PNG output as future work. It cannot implement the ticket's
  `--render <composition> -o <output>` child contract. Restoring those files
  unchanged would therefore leave the wrapper apparently integrated but
  functionally unable to honor composition/output arguments.
- No `chronon.json` exists in the tracked workspace. The proposed subprocess
  contract has no production template/binary to target, so host verification
  of the generated standalone project remains pending.
- P1 now parses the project manifest, validates `name`, resolves
  `build/<name>`, and dispatches `--render <composition> -o <output>` without
  a shell. `tests/cli/test_chronon_project_render_dispatch.cpp` verifies argv
  preservation (including spaces), child exit propagation, project-root working
  directory, unsafe names, and the missing-build diagnostic using a temporary
  executable.
- End-to-end invocation of the built `chronon3d_cli render` command was checked
  against a temporary project directory. It exited 0; the child received, as
  separate argv entries, `--render`, `Hello World`, `-o`, `output file.mp4`,
  and its working directory was the project root. A second invocation with no
  child exited nonzero and printed the required build-first instruction. This
  verifies the CLI callback and process boundary, but not the absent production
  template binary.
- The ticket remains OPEN for real template integration: the referenced
  standalone template/binary contract is absent, so the build-produced project
  executable cannot be tested from this checkout. No external render or GPU
  certification is claimed.

## Local CPU re-run — 2026-10-11

The current `linux-video-test` CLI test binary ran both project-dispatch cases:
`chronon.json project dispatch preserves subprocess arguments` and
`chronon.json project dispatch reports missing build and rejects path names`.
Result: **2/2 cases and 11/11 assertions passed**. This reconfirms the manifest
dispatch and fail-closed diagnostics with the current local test binary; it
does not satisfy the remaining real-template integration gate above.

## Revalidation of the template dependency — 2026-10-11

The current `chronon3d_cli --help` lists no `create` subcommand;
`chronon3d_cli create --help` prints the root help, and the tracked checkout
contains neither `templates/basic/chronon.json` nor `templates/basic/src/root.cpp`.
Thus there is no product path in this checkout that can generate the project
whose binary the loader is meant to dispatch to. The subprocess contract is
verified, but closing the real-template gate requires restoring or selecting a
supported project-template command as part of the parent CLI starter-template
scope; a fabricated fixture would only repeat the existing subprocess test.
