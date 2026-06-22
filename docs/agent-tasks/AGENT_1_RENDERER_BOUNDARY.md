# Agent 1 — Renderer/Backend Single Identity

## Missione

Ripristinare un solo confine architetturale tra orchestratore e backend software.

`SoftwareRenderer` deve essere una facade/orchestratore. `SoftwareBackend` deve essere l'implementazione di `graph::RenderBackend`. Il render graph non deve recuperare servizi software tramite `dynamic_cast<SoftwareRenderer*>`.

Branch assegnato:

```bash
git fetch origin
git checkout main
git pull --ff-only origin main
git checkout codex/agent1-renderer-boundary
git rebase origin/main
git status -sb
```

## Problema da chiudere

Lo stato corrente è contraddittorio:

- `tools/check_software_renderer_boundary.sh` richiede che `SoftwareRenderer` non erediti `graph::RenderBackend`;
- lo stesso gate vieta `dynamic_cast<SoftwareRenderer*>`;
- l'header corrente reintroduce la doppia identità;
- il relativo step CI è ancora advisory con `continue-on-error: true`.

Non correggere il gate per accettare lo stato corrente. Correggere il codice affinché il gate passi senza rilassare le invarianti.

## Perimetro consentito

Puoi modificare soltanto i file necessari nelle seguenti aree:

- `include/chronon3d/backends/software/`
- `src/backends/software/`
- `include/chronon3d/runtime/render_runtime.hpp`
- `src/runtime/render_runtime.cpp`
- call site strettamente necessari sotto `src/render_graph/`
- call site strettamente necessari sotto `apps/chronon3d_cli/`
- test mirati relativi a backend, renderer e capability
- `tools/check_software_renderer_boundary.sh`, solo per correggere bug reali dello script, non per indebolire le regole
- `.github/workflows/gates.yml`, esclusivamente per rendere bloccante il gate quando tutte le invarianti passano

Non modificare:

- registry CMake o aggregazione SDK;
- preset vcpkg;
- `content/`;
- `experimental/`;
- documenti canonici di stato, che appartengono all'Agente 2;
- feature non necessarie alla chiusura del confine renderer/backend.

## Lavoro richiesto

1. Fare inventario di tutti i `dynamic_cast<SoftwareRenderer*>`, `SoftwareRenderer&` nelle superfici di processo e dipendenze del render graph dal renderer concreto.
2. Rimuovere `public graph::RenderBackend` dalla dichiarazione di `SoftwareRenderer`.
3. Garantire che `SoftwareBackend` implementi tutte le operazioni e capability richieste da `graph::RenderBackend`, inclusa `capabilities().text_run`.
4. Usare `RenderRuntime::software_backend()` oppure il puntatore canonico `RenderServices::backend` per l'accesso tipizzato. Non creare un nuovo resolver, service locator o bridge parallelo.
5. Eliminare tutti i `dynamic_cast<SoftwareRenderer*>` dalle aree controllate dal gate.
6. Migrare le superfici di processo da `SoftwareRenderer&` a `SoftwareProcessorContext`, `SoftwareBackend&`, `RenderRuntime&` o a un'interfaccia più piccola già esistente.
7. Ridurre `software_renderer.hpp` fino a rispettare le soglie del gate, spostando implementazioni e dipendenze fuori dall'header senza introdurre duplicazioni.
8. Rendere il gate CI bloccante rimuovendo `continue-on-error: true` soltanto dopo che lo script restituisce zero.
9. Aggiungere test di regressione che dimostrino:
   - dispatch corretto di `RenderCapabilities` tramite `RenderBackend*`;
   - rendering testo attivo quando text/Blend2D sono abilitati;
   - assenza della doppia identità;
   - assenza di dipendenza dei processori dal renderer concreto.

## Invarianti obbligatorie

- Nessun executor costruito dentro i nodi.
- Nessun nuovo registry, resolver, sampler, cache o service locator parallelo.
- `RenderRuntime` resta proprietario dell'infrastruttura engine-lifetime.
- `RenderSession` resta proprietaria dello stato per-job/per-sessione.
- Nessun cambiamento di comportamento introdotto soltanto per far passare un hash.
- Non disabilitare test e non trasformare failure in skip.

## Validazione minima

Eseguire e riportare nel PR:

```bash
bash tools/check_software_renderer_boundary.sh
bash tools/check_architecture_boundaries.sh
cmake --preset linux-core-dev
cmake --build --preset linux-core-dev
ctest --preset linux-core-dev-test --output-on-failure
cmake --preset linux-lean-dev
cmake --build --preset linux-lean-dev
ctest --preset linux-lean-dev-test --output-on-failure
```

Aggiungere eventuali test mirati eseguiti singolarmente.

## Criteri di chiusura

Il lavoro è chiuso solo quando:

- tutte le invarianti I1–I5 di `check_software_renderer_boundary.sh` sono verdi;
- lo script restituisce exit code zero;
- il gate CI non è più advisory;
- non esistono `dynamic_cast<SoftwareRenderer*>` nelle superfici controllate;
- `SoftwareRenderer` non eredita più `graph::RenderBackend`;
- il backend software espone correttamente le capability;
- core e lean build/test passano da checkout pulito;
- il diff resta limitato al problema assegnato.

## Consegna Git

```bash
git status -sb
git diff
git add <solo-file-assegnati>
git commit -m "refactor(software): restore renderer backend boundary"
git push origin codex/agent1-renderer-boundary
git log -n 5 --oneline
```

Aprire una PR piccola verso `main`. Non eseguire merge diretto e non pushare su `main`.