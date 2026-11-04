# Chronon 2.0 — chiusura locale 2026-11-03

Sessione senza certificazione CI same-SHA (decisione utente): il perimetro è
“tutto il resto”, con `BLOCKED` e `NOT RUN` dichiarati e mai promossi a PASS.

## Cosa è passato

| Gate | Comando | Esito |
|---|---|---|
| Build C3D Release | `cmake --build build/chronon/linux-video-release --target chronon3d_cli -j12` | `BUILD_EXIT=0`, 269 oggetti, ccache + mold, nessun OOM |
| Test camera | `tests/chronon3d_camera_tests` | 89/89 casi, **67 718** asserzioni, SUCCESS |
| DoF / depth | `tests/chronon3d_render_graph_tests -tc=PR2-RG-DoF*` | 16/16 casi, 979 asserzioni, SUCCESS |
| Lifetime | `tests/chronon3d_render_graph_tests -tc=*lifetime*` | 6/6 casi, 28 asserzioni, SUCCESS |
| Camera + mesh animata (P0-04) | render fixture `03-gltf-animato`, yaw 0,02°, 60 frame raw | copertura **20 100–20 254 px** su tutti i frame (prima: 276 px a f40, 0 a f59) |
| Pixel-truth raw→PNG (P0-08) | `python3 tools/raw_frame_to_png.py --self-test` + estrazione f40 | self-test OK; 1056×588, 20 247 px mesh identici al conteggio sul raw |
| Alpha-cutout video E2E (P0-07) | `python3 tests/e2e/test_p0_07_alpha_cutout_video.py` | 24/24 frame: cutout 0 px sotto soglia e 32 220 px costanti sopra, alpha 0.5 blendato, piano anteriore vince per Z |
| Backface culling volumi chiusi | `chronon3d_renderer_core_tests` + variante box del gate | 206/206 casi, 74 294 asserzioni; box traslucido 191→128/255 (0.5 reale) |
| Template highlight (P1-01/02) | `chronontemplate_web_headline_highlight_pack_test`, `_page_` | 199 check / 0 failures; page test exit 0 |
| Ticket index | riconciliazione ticket del workspace (`--check`) | 69 sheet, index ok |

## Cosa resta dichiaratamente fuori

| Gate | Stato | Motivo |
|---|---|---|
| P0-02 / P0-03 CI same-SHA | `BLOCKED` | escluso per decisione utente; nessun run remoto |
| P0-12 sanitizer/stress | `NOT RUN` | richiede rebuild ASan completo e stress GPU |
| P0-14 fuzz corpus | `NOT RUN` | corpus non disponibile in sessione |
| P1-16 review umana | `BLOCKED` | richiede reviewer |
| P2 lane Vulkan dedicate | `NOT RUN` | host GPU dedicato non disponibile |
| Gate che richiedono `content/` | `NOT RUN` | `CHRONON3D_BUILD_CONTENT=OFF` |

## Riproduzione del fix P0-04

```bash
cd Chronon3d
cmake --build build/chronon/linux-video-release --target chronon3d_cli -j12
./build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli render \
  --plan /tmp/repro/rot.plan.json \
  --assets-root ../ChrononMotion3D/docs/evidence/showcase-animation-20261103 \
  --backend software --start-frame 0 --end-frame 59 \
  --video-sink raw --output /tmp/repro/rot-fixed.rgba -v error
```

Dettaglio causa/fix: `P0-04-camera-mesh-collapse.md`.
