# TICKET-V3-CLI-UNIFICATION-STARTER-TEMPLATE — `chronon create` + `templates/basic/` (Audit §19)

Stato: PARTIAL (Commit 1 of 2 landed 2026-07-14; Commit 2 in progress)

## Problema

Audit §19 (Blocco 4 — Feedback rapido): "Starter progetto: realmente mancante. Non risulta un comando `chronon create`. Questo va aggiunto. `chronon create my-video` + `cd my-video` + `chronon render HelloWorld -o hello.mp4`. Il comando deve soltanto copiare un template versionato: `templates/basic/` con `CMakeLists.txt`, `chronon.json`, `assets/`, `props/`, `src/root.cpp` + `src/hello_world.cpp`."

Constraint: "Cosa non aggiungere: `project registry`, `template runtime`, `template engine`, `package manager custom`, `browser scaffolding`. È un semplice generatore di file."

L'attuale repository ha esempi completi (`examples/getting_started/main.cpp`, `content/examples/text/*.cpp`, `content/showcases/minimalist/*.cpp`) ma NESSUN meccanismo scaffold per un nuovo progetto da zero. L'utente che vuole iniziare un nuovo video deve clonare il repo, capire la struttura, copiare manualmente i file, e scrivere un `CMakeLists.txt` da zero.

## Soluzione Confine (in 2 commit atomici)

### Commit 1 (landed 2026-07-14) — `feat(templates): basic project template for chronon create`

7 file in `templates/basic/`:
- `CMakeLists.txt` (consumer project, `find_package(Chronon3D)` + `Chronon3D::SDK` + C++20, modellato su `tests/install_consumer/CMakeLists.txt`)
- `chronon.json` (project manifest: name, version, assets_root, main, compositions)
- `src/root.cpp` (`int main()` standalone che renderizza via `sdk::RenderEngine`, modellato su `examples/getting_started/main.cpp`)
- `src/hello_world.cpp` (factory `make_hello_world(Project&)` che definisce la composizione "HelloWorld" usando il `SceneBuilder`/`LayerBuilder` canonical facade)
- `assets/.gitkeep` + `props/.gitkeep` (placeholder)
- `README.md` (build instructions + customization guide + forward-point pointer)

Tutti i file usano il placeholder `${PROJECT_NAME}` sostituito atomicamente a copy-time da `chronon create` (singola `std::string::replace` call, non un template engine — audit verbatim "solo copia file").

### Commit 2 (in progress) — `feat(cli): chronon create subcommand`

`apps/chronon3d_cli/commands/create/register_create_commands.cpp`:
- Subcommand: `chronon create <name>` (positional arg = project name)
- Optional: `--template basic` (default, only "basic" for V1)
- Logic: validate name + locate templates dir + `std::filesystem::copy` recursive + `${PROJECT_NAME}` substitution
- Template discovery: compile-time `-DTEMPLATES_DIR=...` baked into binary via `configure_file` + runtime env var `CHRONON3D_TEMPLATES_DIR` fallback
- Wire-up: `command_registry.hpp` + `command_registry.cpp` + `CMakeLists.txt` (configure_file)

## Criteri accettazione

- [x] Commit 1: 7 file template in `templates/basic/` (cat-3 minimal-surface verified).
- [ ] Commit 2: `chronon create my-video` produce un albero `my-video/` con i 7 file + `${PROJECT_NAME}` sostituito da `my-video`.
- [ ] Commit 2: `cmake -B build` in `my-video/` risolve `Chronon3D::SDK` (forward-cond: SDK installato).
- [ ] Commit 2: `cmake --build build` produce un eseguibile `my-video` che renderizza "HelloWorld" via `sdk::RenderEngine`.
- [ ] Commit 2: `tools/wrap_push.sh` GATE_PASS pre-push.
- [ ] Commit 2: `tools/check_main_clean.sh` GATE_PASS post-push.
- [ ] Commit 2: subject envelope ≤ 72 chars.
- [ ] Cat-3 minimal-surface: ZERO new SDK public symbol; ZERO new singleton/registry/resolver/cache (AGENTS.md deny-everywhere preserved); ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved).
- [ ] macchina-verifica DEFERRED-WBH per TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX (env-block glm/magic_enum + build rot on this VPS); workstation build verification pending on a clean host.

## Forward-points (registrati atomicamente)

- `TICKET-ADD-LOADER-FOR-CHRONON-JSON` (P1, NEW): chronon render extension per leggere `chronon.json` di un progetto esterno (manifest: name, version, assets_root, main) e caricare le composizioni (strategia P1: subprocess dispatch a `./build/<name> --render <comp>`, ZERO nuove dipendenze; P2: shared-library load per V0.2; P3: JSON-only spec per V0.3+). Aspirational flow `cd my-video && chronon render HelloWorld -o hello.mp4` (vedi audit spec).

## Cross-link

- `templates/basic/` (creato in Commit 1): il template versionato copiato da `chronon create`.
- `apps/chronon3d_cli/commands/create/register_create_commands.cpp` (Commit 2): il command implementation.
- `apps/chronon3d_cli/command_registry.cpp` (Commit 2): `register_all_commands()` chiama `register_create_commands()`.
- `apps/chronon3d_cli/CMakeLists.txt` (Commit 2): `configure_file` per `templates_dir.hpp` baked-in path.
- `docs/tickets/TICKET-ADD-LOADER-FOR-CHRONON-JSON.md` (creato in Commit 1): forward-point per la full UX aspirazionale.
- TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3 (CLOSED 2026-07-14): parent ticket della Blocco 3.2; questo ticket ne estende la copertura a Blocco 4.2.
- TICKET-V3-CLI-UNIFICATION-PROFILE-HELP (CLOSED 2026-07-14): sibling ticket del Blocco 3.2; stesso pattern Cat-3 minimal-surface.
- AGENTS.md §regole "Fare PR piccole e mirate" (Commit 1 + Commit 2 split, no mescolare refactor indipendenti).
- AGENTS.md §Cat-3 minimal-surface + deny-everywhere rule.

## Stato cronaca commit

- Commit 1 (landed 2026-07-14): `feat(templates): basic project template for chronon create` — 7 file template + forward-point ticket.
- Commit 2 (in progress): `feat(cli): chronon create subcommand` — create command + CMake wire-up.
