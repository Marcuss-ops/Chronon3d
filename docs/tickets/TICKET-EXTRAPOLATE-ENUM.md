# TICKET-EXTRAPOLATE-ENUM — canonical interpolation policies

## Stato: CLOSED — 2026-07-26

Il sampler canonico vive in
`include/chronon3d/animation/easing/interpolate.hpp`.

La pipeline produttiva è:

```text
input → normalizzazione → Extrapolate policy → easing → output range
```

Sono disponibili le policy `Clamp`, `Extend` e `Wrap` tramite
`InterpolateOptions`. Gli overload `Frame`, `FrameRange`, `Vec2` e `Vec3`
delegano alla stessa matematica scalare; non esiste più una seconda
implementazione di progress o `ClampMode` nel codice produttivo.

## Verifica

Il filtro `*interpolate*` di `chronon3d_core_tests` passa 9/9 test e 65/65
assertion. Sono coperti range degenerati, endpoint, policy asimmetriche,
clamp, estensione lineare e wrapping prima/dopo il range.

## Decisione

`InterpolateOptions` è l’unico contratto interno. Non aggiungere enum o
overload locali per il clamping: nuove superfici devono delegare al sampler
canonico.
