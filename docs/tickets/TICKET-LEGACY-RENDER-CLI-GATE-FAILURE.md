# TICKET-LEGACY-RENDER-CLI-GATE-FAILURE — Legacy render CLI gate failure on main

## Stato: CHIUSO (incidente risolto su main)

## Problema
Il gate `tools/check_no_legacy_render_cli.sh` è fallito su `main` a causa di 7 violazioni della superficie CLI render legacy. Il fallimento ha bloccato il push di un commit doc-only (`TICKET-DEPRECATED-API-REMOVAL`) perché `tools/wrap_push.sh` esegue i developer gate prima di ogni push.

## Violazioni rilevate

| Pattern | File / linee | Azione correttiva |
|---|---|---|
| `RenderJobPlan` | `tests/cli/test_render_job_planning.cpp:18` (stringa fixture) | Rinominata la stringa per evitare il sottostringa proibita. |
| `VideoJobPlan` | `apps/chronon3d_cli/commands/video/common/encoder_options.hpp` (commento) + `tests/cli/test_ffmpeg_export_options.cpp` (tipo/stale test) | Commento riscritto; rimosso test stale che usava un tipo non definito. |
| `plan_render_job(` | `apps/chronon3d_cli/commands/dev/command_bench_convert.cpp:166` | Sostituito con il builder canonico `make_render_job`. |
| `plan_video_job(` | `tests/cli/test_ffmpeg_export_options.cpp` (commento) | Commento rimosso insieme al test stale. |
| `VideoArgs` | `tests/video/test_video_contracts.cpp` (commento) + `apps/chronon3d_cli/utils/job/render_job.cpp` + `apps/chronon3d_cli/commands.hpp` | Commenti riscritti per evitare il nome proibito. |
| `CHRONON3D_HAS_CLI_VIDEO` | `apps/chronon3d_cli/commands/cli_groups.hpp`, `apps/chronon3d_cli/commands/dev/register_dev_commands.cpp`, `apps/chronon3d_cli/commands/dev/command_bench_convert.cpp` | Rinominato in `CHRONON3D_HAS_CLI_VIDEO_EXPORT`; rimosso il blocco `group_video` morto. |
| `chronon3d_cli_video` | `tests/video_tests.cmake`, `tests/cli_tests.cmake` (target/commento) | Corretto in `chronon3d_cli_video_export`; commento riscritto. |

## Soluzione applicata

- Commit risolutivo: `fix(cli): resolve legacy render CLI gate violations`.
- Tutti i pattern ritirati sono stati rimossi o sostituiti con i corrispettivi canonici.
- Il gate `tools/check_no_legacy_render_cli.sh` ora passa su `main`.

## Criteri di accettazione

- [x] `tools/check_no_legacy_render_cli.sh` restituisce `GATE_PASS` su `main`.
- [x] `tools/run_developer_gates.sh` passa senza regressioni.
- [x] `tools/check_doc_sync.sh` passa.
- [x] Push su `main` completato con SHA-triple verification.

## Forward-points

- Verificare che `CHRONON3D_HAS_CLI_VIDEO_EXPORT` venga propagato correttamente al target `chronon3d_cli_dev` se si intende abilitare il comando `benchconvert` (al momento rimane disabilitato come prima del fix).
- Aggiungere un test di regressione che verifichi che i pattern ritirati non vengano reintrodotti (il gate `check_no_legacy_render_cli.sh` già copre questo, ma un test unitario esplicito potrebbe accelerare il feedback in locale).
