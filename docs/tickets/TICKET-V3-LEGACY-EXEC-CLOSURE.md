# TICKET-V3-LEGACY-EXEC-CLOSURE — Unified RenderJob closure

## Stato

**CLOSED — Strategy X + Strategy Y complete (2026-07-15).**

La macchina-verifica completa resta separata dallo stato di implementazione: il codice è chiuso e protetto da gate, ma una nuova baseline WBH non è ancora stata certificata.

## Problema risolto

Il repository manteneva più superfici per lo stesso lavoro:

- `RenderJobPlan` per still e sequence;
- `VideoJobPlan` per video;
- `plan_render_job` e `plan_video_job`;
- adapter mutabile `execute_render_job(registry, job)`;
- comandi separati `still`, `video` e `video-camera`;
- target e macro CMake dedicati agli alias legacy.

Questa duplicazione permetteva divergenze tra preview, render normale, video e daemon.

## Soluzione atterrata

Il percorso unico è ora:

```text
RenderArgs
  -> make_render_job(...)
  -> RenderJob
  -> execute_render_job(const RenderJob&)
  -> renderer/export backend
```

`RenderJob` copre:

- still;
- sequence;
- frame selezionati non contigui per preview/contact sheet;
- video tramite exporter FFmpeg interno.

Gli exporter video restano backend di basso livello. Non possiedono un secondo job descriptor o executor.

## Legacy eliminata

Sono stati rimossi:

- `RenderJobPlan`;
- `VideoJobPlan`;
- `plan_render_job(...)`;
- `plan_video_job(...)`;
- `make_video_render_job(...)`;
- l'adapter mutabile `execute_render_job(registry, job)`;
- `StillArgs`, `VideoArgs`, `VideoCameraArgs`;
- `command_still`, `command_video`, `command_video_camera`;
- `register_video_commands` e `group_video`;
- `CHRONON3D_BUILD_CLI_VIDEO` e `CHRONON3D_HAS_CLI_VIDEO`;
- i file `video_job_plan.hpp/.cpp`;
- il target `chronon3d_cli_video`.

## Barriere anti-regressione

### Gate statico bloccante

`tools/check_no_legacy_render_cli.sh` è registrato nei developer gate e fallisce se riappaiono simboli, file, target o macro ritirati.

### Smoke runtime WBH

`tools/verify_cli_render_surface_linux.sh` verifica sul binario reale che:

- `render`, `preview` e `create` siano presenti;
- `still` e `video` siano assenti;
- le opzioni canoniche di render e contact sheet siano esposte.

## Criteri di accettazione

- [x] un solo job descriptor;
- [x] un solo executor immutabile;
- [x] video dispatch dentro il percorso `RenderJob`;
- [x] alias e planner rimossi fisicamente;
- [x] CMake senza target/macro alias;
- [x] gate statico registrato nella catena developer;
- [x] smoke runtime registrato nella catena WBH;
- [ ] build, test e product certification completi sullo stesso commit WBH.

## Vincoli rispettati

- nessun nuovo singleton, registry, resolver o cache;
- nessuna preview pipeline parallela;
- nessun nuovo SDK pubblico;
- core headless e CPU-first invariato;
- nessun browser, GUI o plugin ABI.

## Riferimenti

- `include/chronon3d/timeline/render_job.hpp`
- `apps/chronon3d_cli/utils/job/render_job.hpp`
- `apps/chronon3d_cli/utils/job/render_job_execute.cpp`
- `tools/check_no_legacy_render_cli.sh`
- `tools/verify_cli_render_surface_linux.sh`
- `docs/CURRENT_STATUS.md`
- `docs/RELEASE_GATE.md`
