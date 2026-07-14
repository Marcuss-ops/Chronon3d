# TICKET-ADD-LOADER-FOR-CHRONON-JSON — chronon.json loader for `chronon render`

Stato: OPEN (forward-point of TICKET-V3-CLI-UNIFICATION-STARTER-TEMPLATE / Blocco 4.2)

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

- [ ] `chronon render HelloWorld -o hello.mp4` in CWD con `chronon.json` →
      delega a `./build/<name> --render HelloWorld -o hello.mp4` (subprocess).
- [ ] Se `./build/<name>` manca → errore user-friendly: "Build the project
      first: `cmake -B build && cmake --build build`".
- [ ] ZERO nuove dipendenze (no shared library loader, no in-process compiler).
- [ ] ZERO nuovi SDK symbol; ZERO nuovi singleton/registry/resolver/cache
      (eccezione: il dispatcher stesso, già parte di `command_render`).
- [ ] macchina-verifica REAL-GREEN-PASS su TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX-clean host.

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
