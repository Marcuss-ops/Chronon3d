# C06 — Optical Studio v3, 150 frame strict-native (2026-11-03)

**Stato: PASS locale del render/output.** Non è il PASS end-to-end del ticket:
review umana e CI same-SHA restano esterni e bloccati.

## Comando

Eseguito dalla root del workspace, senza `CHRONON3D_VULKAN_SINGLE_FLIGHT`:

```bash
CHRONON3D_VULKAN_VALIDATION=1 \
  Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli render \
  --plan Chronon3d/docs/evidence/camera-closure-final/optical_current_pair_v3.plan.json \
  --assets-root "$PWD/Chronon3d" --backend vulkan \
  --gpu-hot-path-mode require_gpu_native --hardware nvenc --encoder-backend native \
  --start-frame 0 --end-frame 149 \
  -o Chronon3d/docs/evidence/c06-current-20261103/optical_v3_c06_150f_1080p.mp4
```

- Piano: `camera-closure-final/optical_current_pair_v3.plan.json`
- Backend: Vulkan, hot path `require_gpu_native`
- Encoder: NVENC H.264 nativo
- Validation layer: attivo
- Single-flight: **non impostato**
- Host: RTX A4000, Linux

## Output verificato

| Controllo | Risultato |
|---|---|
| Exit code | `0` |
| Codec / pixel format | H.264 / `yuv420p` |
| Dimensioni | 1920 × 1080 |
| Frame rate | 30/1 |
| Durata | 5.000 s |
| Frame dichiarati / letti | `150 / 150` |
| Decode completo ffmpeg | exit 0, log vuoto |
| `blackdetect` (d=0,1; pix_th=0,10) | 0 intervalli neri |
| Vulkan `Validation Error` / `VUID-` | **0** |
| Warning `detached reused extent` | 2.392 (noti, non validation error) |
| Encoder | 150 frame accettati, 150 packet, PTS 0..149 |
| SHA-256 video | `cffa74d77d3360abd1d0f29588fa72c5ca39aa9ffa4406da850effcde51664c3` |

Timing: `render_ms=320296.327398` nel sidecar; `execute_ms=342126.9` nel log.
Il contact sheet `c06-contact-sheet.jpg` campiona i frame 0/30/60/90/120/149;
l'ispezione locale mostra il passaggio di fuoco NEAR/MID/FAR, senza frame neri o
vuoti.

## Limiti

- La review visiva dell'intero video da parte di un reviewer nominato non è
  disponibile.
- La CI verde sullo stesso pair di SHA non è disponibile; l'albero è dirty.
- I 2.392 warning di lifecycle non sono stati promossi a PASS: restano il
  rumore noto documentato dal ticket camera.
- Questo run chiude solo la riesecuzione C6 richiesta; non chiude §8.4 né il
  ticket.
