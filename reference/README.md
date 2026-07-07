# Reference — AE-parity mock + engine output

This directory hosts the AE-parity referee's input/output PNGs:

- `after_effects/scene_NNN_frame_NN.png` — AE-side mock (manually committed,
  canonical reference; AE = Adobe After Effects).
- `chronon3d/scene_NNN_frame_NN.png`     — engine output (gitignored via
  `reference/chronon3d/*.png` in `.gitignore`, regenerated per ctest run).

## Contract

Per frame: `MAE < 5/255` (mean absolute error) AND `PSNR > 30dB` (perceptual
color metric). Anti-greenwashing: >= 10 of 15 cinematic scenes must PASS
before any "AE-like" claim is allowed.

## Commands

```bash
tools/ae_parity_referee.sh scaffold    # create dirs (idempotent)
tools/ae_parity_referee.sh             # human-readable table
tools/ae_parity_referee.sh --json      # JSON output for CI
AE_PARITY_MIN_PASS=15 tools/ae_parity_referee.sh  # tighten the gate
```

## Cross-link

- TICKET-AE-PARITY-DRIVER (this script + ticket doc)
- TICKET-AE-PARITY-SUITE (the 20 cinematic scenes that fill reference/after_effects/)
- TICKET-AE-PARITY-FLOOR (288+ PNG floor deliverable)
