# Chronon3D — Stabilization and Improvement Plan

Questo piano trasforma i principali problemi architetturali e operativi del repository in work package indipendenti, verificabili e adatti a pull request piccole.

## Obiettivo

Portare Chronon3D da baseline pre-stabile a motore headless CPU-first con:

- build ripetibile;
- rendering deterministico;
- scope di esecuzione espliciti;
- un solo confine SDK pubblico;
- registrazione centralizzata dei moduli CMake;
- renderer software più sottile;
- documentazione sincronizzata con lo stato reale;
- profili di dipendenza e CI coerenti.

## Regole operative

Ogni work package deve rispettare queste regole:

- partire da `origin/main` aggiornato;
- usare un branch dedicato `codex/<nome-blocco>`;
- risolvere un solo problema principale;
- cercare e riusare il codice esistente prima di aggiungere nuove strutture;
- non introdurre registry, resolver, sampler, cache o executor paralleli;
- aggiornare o aggiungere test mirati;
- non modificare file fuori perimetro senza motivazione esplicita;
- fare rebase frequente su `origin/main`;
- verificare `git status -sb` e il diff prima del push;
- dopo il push controllare gli ultimi commit;
- aprire la PR solo con check pertinenti verdi.

## Ordine consigliato

1. [Baseline verde e record di validazione](01-baseline-green.md)
2. [Determinismo scheduler e rendering](02-determinism.md)
3. [ExecutionScope e isolamento Precomp](03-execution-scope-and-precomp.md)
4. [Registry centrale dei moduli CMake](04-cmake-module-registry.md)
5. [Confine SDK e install consumer unico](05-sdk-install-boundary.md)
6. [SoftwareRenderer come facade sottile](06-software-renderer-facade.md)
7. [Documentazione, ADR e sincronizzazione stato](07-documentation-and-adrs.md)
8. [Profili dipendenze e matrice build](08-dependency-profiles.md)

## Master TODO

### P0 — stabilità

- [ ] Identificare un commit baseline e registrare SHA, preset e toolchain.
- [ ] Ottenere build verde su core, lean e no-content.
- [ ] Ottenere `chronon3d_tests_fast` verde senza skip non tracciati.
- [ ] Rendere obbligatorio il consumer SDK esterno.
- [ ] Acquisire golden hash reali per i test di determinismo.
- [ ] Riattivare composite e tile determinism.
- [ ] Introdurre `ExecutionScope` root, tile e precomp.
- [ ] Eliminare fallback di identità Precomp che possono aliasare sibling node.

### P1 — manutenibilità

- [ ] Registrare ogni modulo CMake una sola volta.
- [ ] Eliminare le liste duplicate di OBJECT target, install target e SDK object.
- [ ] Usare un solo progetto install-consumer e un solo orchestratore.
- [ ] Ridurre le responsabilità dirette di `SoftwareRenderer`.
- [ ] Spostare la cronologia lunga dai sorgenti verso ADR e changelog.

### P2 — efficienza e chiarezza

- [ ] Definire profili core, motion, video ed extended.
- [ ] Ridurre le feature predefinite non necessarie al rendering motion graphics.
- [ ] Sincronizzare `STATUS`, `ROADMAP`, `NEXT_STEPS` e ticket dopo ogni chiusura.
- [ ] Misurare tempi di configure, build, test, install e render.

## Criterio di completamento globale

Il piano può essere considerato completato solo quando esiste un commit identificato per cui siano registrati e riproducibili:

```text
architecture gates
core build
lean build
no-content build
fast tests
scheduler determinism
precomp isolation and concurrency
install and external consumer
representative render output
```

Il progetto non deve essere descritto come SDK stabile o release-ready prima di questa prova.