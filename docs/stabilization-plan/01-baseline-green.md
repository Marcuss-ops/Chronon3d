# Baseline verde

## Stato reale

La baseline complessiva non è ancora verde.

- [x] Build `linux-core-dev` verde.
- [x] Build `linux-lean` verde.
- [ ] Build `linux-lean-dev` verde.
- [ ] Test veloci verdi: 704/707, con 3 failure.
- [ ] Gate architetturali verdi: restano 2 violazioni.
- [ ] Consumer SDK esterno verde.
- [x] Prima osservazione documentata con SHA e toolchain.

Un errore pre-esistente non rende verde una suite. Il checkbox può essere chiuso solo con exit code zero.

## Osservazione registrata

**Data:** 2026-06-21  
**SHA osservato:** `f7468355`  
**Toolchain:** CMake 4.2.3, GCC 15.2.0, Ninja 1.13.2, x86_64  
**vcpkg:** baseline 2026-05-27

Questa osservazione è storica e non sostituisce una nuova validazione sul `main` corrente.

## Blocchi da chiudere in ordine

### 1. Tre test falliti

- [ ] Correggere `test_render_session_reset_and_isolation` nel percorso concorrente.
- [ ] Correggere il primo test scene che chiama `RenderRuntime::backend()` prima di `attach_backend()`.
- [ ] Correggere il secondo test scene con lo stesso contratto backend non inizializzato.
- [ ] Rieseguire la singola suite dopo ogni fix.
- [ ] Rieseguire l'intero `chronon3d_tests_fast` da build pulita.
- [ ] Registrare il nuovo totale soltanto quando è 707/707 o equivalente senza failure.

### 2. Due violazioni architetturali

- [ ] Identificare le due violazioni in `render_session.hpp`.
- [ ] Stabilire se sono include vietati, simboli ritirati o ownership duplicata.
- [ ] Correggere il codice, non indebolire il gate.
- [ ] Aggiungere o aggiornare un test canary per impedire la regressione.
- [ ] Eseguire `chronon3d_architecture_includes_boundary` con exit code zero.
- [ ] Eseguire anche tutti gli script in `tools/check_*architecture*`.

### 3. Preset `linux-lean-dev`

- [ ] Riprodurre il primo errore da build pulita.
- [ ] Separare errori text, CLI, authoring e renderer.
- [ ] Correggere un solo lineage per commit.
- [ ] Verificare le guardie CMake TEXT/BLEND2D.
- [ ] Verificare firme `FontEngine`, typewriter e costruttori `SoftwareRenderer`.
- [ ] Ricostruire l'intero preset senza affidarsi a oggetti precedenti.

### 4. Consumer SDK

- [ ] Correggere propagazione di `CMAKE_TOOLCHAIN_FILE` o `CMAKE_PREFIX_PATH`.
- [ ] Verificare risoluzione di `spdlog`, `fmt` e dipendenze transitive.
- [ ] Installare in prefix temporaneo vuoto.
- [ ] Configurare un consumer esterno con solo `find_package(Chronon3D)`.
- [ ] Collegare soltanto `Chronon3D::SDK`.
- [ ] Compilare ed eseguire il consumer.
- [ ] Usare lo stesso percorso in locale, CTest e CI.

## Comandi minimi da registrare

La chiusura deve registrare i comandi equivalenti per:

```text
architecture checks
linux-core-dev
linux-lean
linux-lean-dev
linux-ci-nocontent
chronon3d_tests_fast
install_consumer_ci
external consumer run
```

## Completato quando

- [ ] Tutti i target obbligatori restituiscono exit code zero.
- [ ] Nessun test fallisce.
- [ ] Nessun gate architetturale fallisce.
- [ ] Il consumer installato configura, compila ed esegue.
- [ ] SHA, toolchain, comandi e risultati del `main` validato sono registrati.
