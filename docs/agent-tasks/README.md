# Chronon3D Agent Tasks

Incarichi operativi correnti:

1. [`AGENT_1_RENDERER_BOUNDARY.md`](AGENT_1_RENDERER_BOUNDARY.md) — single identity renderer/backend, processor context e gate renderer.
2. [`AGENT_2_CMAKE_SDK_BASELINE.md`](AGENT_2_CMAKE_SDK_BASELINE.md) — registry CMake, toolchain, external SDK consumer e baseline verificata.

Ordine di integrazione:

1. merge Agente 1;
2. rebase Agente 2 su `origin/main`;
3. validazione completa;
4. merge Agente 2.

Ogni agente deve partire da `main` aggiornato, restare nel proprio perimetro e aprire una PR mirata.