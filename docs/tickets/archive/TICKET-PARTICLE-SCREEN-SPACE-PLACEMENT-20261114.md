# TICKET-PARTICLE-SCREEN-SPACE-PLACEMENT — full-canvas emitter still renders in the top-left quadrant

**Stato:** CLOSED (2026-11-14)
**Priorità:** P1
**Ambito:** ChrononTemplate 09/10 fixture + screen-space particle placement
**Origine:** ChrononTemplate motion-design stress suite, case 09 (`09_particles_add_screen`) and case 10 (`10_zsort_intersect`)

## Causa

`sample_particles()` produces emitter-local positions centred on `(0,0)`. The
suite authoring already placed the particle layers at the canvas centre
(`[W/2, H/2]`), but `normaliseScreenSpace()` converted **every** screen-space
layer from centre to top-left, including particles. That moved the emitter by
`-W/2,-H/2`; only the positive quadrant of the cloud landed in the frame.

## Fix

`ChrononTemplate/tests/motion_design_stress.cpp`: `normaliseScreenSpace()` now
skips `type == "particles"`, alongside text. Particle layers keep their centred
basis; images/shapes/plates still use top-left. The typed materializer and the
legacy path both keep the emitter-local instances; SourceNode applies the
resolved placement exactly once.

## Verifica

- Case 09, frame 60, software: bbox **`(0,0)-(1919,1071)`**, 1.165% non-black,
  instead of `(0,0)-(960,599)`.
- Case 10, frame 60, software: bbox **`(0,0)-(1919,1079)`**, 99.999% non-black;
  paper/cutout and both particle bands compose over the full canvas.
- Motion-design suite: **403 checks, 0 failures** (two new regression locks on
  the particle centre through `normaliseScreenSpace`).
- The 120-frame 09/10 MP4s still use the software fallback; Vulkan particle
  performance is tracked separately and is not part of this placement ticket.
