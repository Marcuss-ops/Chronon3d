# Agent 2 — CMake Registry, SDK Boundary e Baseline Verificata

## Missione

Rendere la build ripetibile, eliminare la registrazione duplicata dei moduli CMake, chiudere il confine SDK e produrre una baseline documentale basata esclusivamente su risultati osservati.

Branch assegnato:

```bash
git fetch origin
git checkout main
git pull --ff-only origin main
git checkout codex/agent2-cmake-sdk-baseline
git rebase origin/main
git status -sb
```

Puoi lavorare in parallelo all'Agente 1, ma prima della validazione finale devi fare rebase sul `main` che contiene il lavoro renderer/backend approvato.

## Problema da chiudere

Lo stato corrente presenta quattro rischi collegati:

- le OBJECT library sono elencate manualmente in più blocchi CMake;
- dimenticare un target in una delle liste produce errori di link o installazione;
- preset e workflow usano riferimenti vcpkg non chiaramente unificati;
- documenti e messaggi di commit possono dichiarare stati diversi dai risultati effettivamente verificati.

L'obiettivo non è aggiungere nuovi profili o funzionalità, ma creare una sola fonte di verità per moduli, SDK e attestazione della baseline.

## Perimetro consentito

Puoi modificare:

- `CMakeLists.txt`
- `src/CMakeLists.txt`
- `src/**/CMakeLists.txt` solo quando necessario alla registrazione canonica
- `cmake/`
- `CMakePresets.json`
- `vcpkg.json`
- `tools/install_consumer_test.sh`
- script di validazione build/install pertinenti
- `.github/workflows/gates-full-validation.yml`
- la parte install/consumer delle workflow, evitando il gate renderer dell'Agente 1
- `tests/install_consumer/`
- documenti canonici: `docs/STATUS.md`, `docs/NEXT_STEPS.md`, `docs/ROADMAP.md`, `docs/stabilization-plan/README.md` e piani direttamente interessati

Non modificare:

- implementazione renderer/backend;
- `SoftwareRenderer`, `SoftwareBackend`, processori software o call site del render graph;
- `content/` per correggere errori funzionali;
- `experimental/`;
- API pubbliche non necessarie al confine SDK.

## Lavoro richiesto

### A. Registry CMake centrale

1. Inventariare tutte le `add_library(... OBJECT ...)` del progetto.
2. Introdurre una funzione o registry CMake canonico per registrare ogni modulo una sola volta.
3. Dal registry devono derivare, senza liste duplicate:
   - aggregazione in-tree;
   - oggetti del target SDK;
   - archivio installabile;
   - export/install;
   - dipendenze condizionali per feature.
4. Aggiungere un controllo che fallisca quando una OBJECT library Chronon3D non è registrata.
5. Non creare registry differenti per core, graph e SDK se possono essere espressi nello stesso modello.

### B. Toolchain e profili vcpkg

1. Definire una sola sorgente canonica per la root vcpkg e per il toolchain file.
2. Allineare preset locali e GitHub Actions.
3. Conservare i profili core, lean e full-validation senza introdurre dipendenze pesanti nel core.
4. Verificare build con text/Blend2D OFF e ON.
5. Non cambiare versioni o dipendenze soltanto per aggirare errori di codice.

### C. Confine SDK

1. Installare Chronon3D in un prefix temporaneo o locale esplicito.
2. Configurare un consumer realmente esterno usando esclusivamente `find_package(Chronon3D CONFIG REQUIRED)` e `Chronon3D::SDK`.
3. Verificare che il consumer non dipenda da path sorgente, OBJECT target interni o header privati.
4. Testare almeno:
   - core/no-text;
   - text + Blend2D;
   - no-content;
   - install, configure, build e run.
5. Un errore pre-esistente deve restare failure visibile oppure essere isolato con ticket preciso; non trasformarlo in successo.

### D. Baseline e documentazione

1. Eseguire le build/test richieste da checkout pulito.
2. Registrare commit, preset, comandi e risultati.
3. Aggiornare i documenti canonici soltanto con esiti osservati.
4. Rimuovere numeri storici come conteggi test o failure se non sono stati riconfermati sul commit corrente.
5. Non dichiarare `main` verde, release-ready o SDK stabile fino a quando tutti i gate richiesti sono realmente passati.
6. Documentare separatamente:
   - build verde;
   - suite test verde;
   - architecture gate verde;
   - install consumer verde;
   - full validation verde.

## Invarianti obbligatorie

- Una nuova OBJECT library deve essere registrata una sola volta.
- Build, install ed export devono derivare dalla stessa fonte.
- Nessun file generato o directory di build nel commit.
- Nessuna modifica al codice runtime per nascondere problemi della build.
- Nessuna rimozione di test per ottenere una baseline verde.
- Nessun claim documentale senza comando e risultato ripetibile.

## Validazione minima

Eseguire e riportare nel PR:

```bash
cmake --preset linux-core-dev
cmake --build --preset linux-core-dev
ctest --preset linux-core-dev-test --output-on-failure

cmake --preset linux-lean-dev
cmake --build --preset linux-lean-dev
ctest --preset linux-lean-dev-test --output-on-failure

cmake --preset linux-ci-nocontent
cmake --build build/chronon/linux-ci-nocontent
ctest --test-dir build/chronon/linux-ci-nocontent --output-on-failure

cmake --preset linux-full-validation
cmake --build --preset linux-full-validation
ctest --preset linux-full-validation-test --output-on-failure

bash tools/install_consumer_test.sh
```

Eseguire anche il controllo del registry CMake introdotto dal lavoro.

## Criteri di chiusura

Il lavoro è chiuso solo quando:

- ogni OBJECT library è registrata una volta sola;
- l'aggiunta di un target non richiede modifiche manuali in liste parallele;
- preset e workflow usano una toolchain vcpkg coerente;
- il consumer installato configura, compila e gira fuori dalla source tree;
- core, lean, no-content e full-validation hanno risultati riportati onestamente;
- `STATUS.md`, `NEXT_STEPS.md`, `ROADMAP.md` e stabilization plan descrivono lo stesso stato;
- non restano claim verdi privi di attestazione;
- il diff non include refactor renderer/backend appartenenti all'Agente 1.

## Consegna Git

Prima della validazione conclusiva:

```bash
git fetch origin
git rebase origin/main
git status -sb
```

Poi:

```bash
git diff
git add <solo-file-assegnati>
git commit -m "build(cmake): centralize modules and verify sdk boundary"
git push origin codex/agent2-cmake-sdk-baseline
git log -n 5 --oneline
```

Aprire una PR verso `main` dopo il merge o il rebase del lavoro dell'Agente 1. Non pushare direttamente su `main`.