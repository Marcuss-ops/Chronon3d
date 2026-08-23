# TICKET-V3-CLI-UNIFICATION-STARTER-TEMPLATE — `chronon create`

## Stato

**DONE (2026-07-15).** Runtime/build smoke resta da rieseguire sul working build host insieme alla nuova baseline.

## Soluzione atterrata

`chronon create <name>` copia il template versionato `templates/basic/` in una nuova directory.

```bash
chronon create my-video
cd my-video
cmake -B build
cmake --build build
./build/my-video
```

Il template contiene:

```text
templates/basic/
├── CMakeLists.txt
├── chronon.json
├── README.md
├── assets/.gitkeep
├── props/.gitkeep
└── src/
    ├── root.cpp
    └── hello_world.cpp
```

## Comportamento

- valida nomi alfanumerici con `-` e `_`;
- supporta `--template basic`;
- supporta `--force`;
- risolve la directory template da `CHRONON3D_TEMPLATES_DIR` oppure dal path baked da CMake;
- copia ricorsivamente file e directory;
- sostituisce soltanto `${PROJECT_NAME}`;
- esegue rollback della directory parziale se la copia fallisce.

## Vincoli rispettati

Non sono stati introdotti:

- project registry;
- template runtime;
- template engine;
- package manager custom;
- browser scaffolding;
- nuovi simboli SDK;
- singleton, registry, resolver o cache.

## Criteri di accettazione

- [x] template `basic` versionato;
- [x] comando `create` registrato nella CLI core;
- [x] placeholder derivato dal literal, senza magic length;
- [x] directory template baked correttamente da CMake;
- [x] rollback su copia parziale;
- [x] smoke della superficie registrato in `verify_cli_render_surface_linux.sh`;
- [ ] build del progetto generato rieseguita nella nuova baseline WBH.

## Forward point separato

Il flusso:

```bash
cd my-video
chronon render HelloWorld -o hello.mp4
```

richiede il loader/dispatcher di progetto esterno tracciato da `TICKET-ADD-LOADER-FOR-CHRONON-JSON`. Non fa parte del semplice generatore di file e non deve trasformare `create` in un package manager o template runtime.
