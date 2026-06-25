# Chronon3D Agent Instructions

Questo file è il punto di ingresso operativo per ogni agente che lavora nel repository.

## Missione del progetto

Chronon3D è un motore C++20 headless, Linux-only e CPU-first per motion graphics e compositing programmabile.

Priorità attuale:

1. camera cinematografica realmente funzionante;
2. animazioni testuali realmente temporali;
3. composizioni e showcase renderizzabili dalla CLI;
4. rendering deterministico e riproducibile;
5. pulizia progressiva del legacy soltanto dopo un percorso reale funzionante.

Non introdurre GUI, browser, supporto Windows o dipendenze GPU-first nel core.

## Prima di iniziare

```bash
git fetch origin
git checkout main
git pull --ff-only origin main
git status -sb
```

Su `main` può scrivere un solo agente alla volta. Gli altri agenti possono analizzare, preparare patch o eseguire test, ma devono riallinearsi con `git pull --ff-only origin main` prima di modificare o committare.

Non creare branch o PR salvo richiesta esplicita.

## Documenti canonici

- `README.md` — ingresso e quick start;
- `docs/CURRENT_STATUS.md` — unica fonte dello stato corrente;
- `docs/ROADMAP.md` — milestone ancora attive;
- `docs/RELEASE_GATE.md` — criteri tecnici di validazione;
- `docs/FOLLOWUP_TICKETS.md` — problemi ancora aperti;
- `docs/adr/` — decisioni architetturali;
- `docs/CAMERA_FEATURE_MATRIX.md` — dettaglio camera;
- `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` — dettaglio testo.

Non creare nuovi documenti di stato paralleli. Quando cambia lo stato, aggiornare `docs/CURRENT_STATUS.md` o il documento tecnico specifico.

## Regole architetturali

- Cercare prima il codice esistente.
- Non duplicare registry, resolver, sampler, cache, service locator o pipeline.
- Ogni nuova feature deve entrare nel percorso canonico già esistente.
- Camera canonica: `CameraDescriptor → compile_camera() → CameraProgram`.
- Testo canonico: `TextDocument/TextSpec → layout → animator stack → renderer`.
- Render canonico: `RenderGraph → FrameGraphCompiler → CompiledFrameGraph → GraphExecutor`.
- Non aggiungere correzioni speciali soltanto per uno showcase.
- Non indebolire gate o test per nascondere un errore.
- Non trasformare failure in skip.
- Non committare build, output, video, PNG, cache o file generati.
- Limitare ogni modifica a un problema chiaro e ai file necessari.

## Flusso di consegna su main

```bash
git status -sb
git diff
# eseguire almeno i test mirati del modulo toccato
git add <solo-file-modificati>
git commit -m "<tipo(scope): descrizione chiara>"
git push origin main
git log -n 5 --oneline
```

Dopo ogni push verificare che il commit remoto contenga davvero i file previsti.

## Quando un file sembra mancare

1. controllare `git status -sb`;
2. controllare `git rev-parse HEAD`;
3. eseguire `git fetch origin`;
4. confrontare `HEAD` con `origin/main`;
5. aggiornare il checkout prima di creare sostituti.

Non creare file con nomi simili per sostituire documenti o componenti già esistenti.