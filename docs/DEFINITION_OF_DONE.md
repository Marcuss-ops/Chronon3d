# Definition Of Done

Una PR Chronon3D e' davvero finita solo se passa questi blocchi:

1. `git status -sb`, `git diff --check` e `git diff --stat` puliti.
2. Build `linux-dev` o `linux-debug` ok.
3. Build `linux-release` o `linux-release-full` ok se il cambiamento tocca performance, timing o render path.
4. Test mirati ok.
5. Render smoke ok.
6. Benchmark prima/dopo incluso quando il lavoro tocca performance.
7. Telemetry coerente con il cambiamento.
8. Nessuna regressione visiva evidente.
9. Nessun file fuori scope nel commit.
10. Report finale con cosa e' migliorato, di quanto, e come si sa che non e' stato rotto altro.

Per fix sul rendering immagine o sul graph, aggiungere anche:

1. `images_sampled`, `cache_hits`, `dirty_full_fallbacks` e `render_phase_events` controllati.
2. Release benchmark su un preset rappresentativo.
3. Un output visivo di smoke render preservato come riferimento.
