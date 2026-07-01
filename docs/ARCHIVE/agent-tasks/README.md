# Chronon3D Agent Tasks

Incarichi operativi correnti:

1. [`AGENT_1_RENDERER_BOUNDARY.md`](AGENT_1_RENDERER_BOUNDARY.md) — single identity renderer/backend, processor context e gate renderer.
2. [`AGENT_2_CMAKE_SDK_BASELINE.md`](AGENT_2_CMAKE_SDK_BASELINE.md) — registry CMake, toolchain, external SDK consumer e baseline verificata.
3. [`AGENT_4_VERIFY_VISUAL_INTEGRATION.md`](AGENT_4_VERIFY_VISUAL_INTEGRATION.md) — end-to-end verification (6 frame chiave, A4.1..A4.6 gates, contact sheet PNG), binario `chronon3d_cinematic_camera_showcase_tests`.

Ordine di integrazione:

1. merge Agente 1;
2. rebase Agente 2 su `origin/main`;
3. validazione completa;
4. merge Agente 2;
5. Agente 4 lavora direttamente su `main` dopo che Agente 1+2 sono integrati (`chronon3d_content` disponibile, preset `linux-ci` pronto).

Ogni agente deve partire da `main` aggiornato, restare nel proprio perimetro e aprire una PR mirata (eccezione: Agente 4 per commit doc-only + showcase test binary atomico, già pianificato).