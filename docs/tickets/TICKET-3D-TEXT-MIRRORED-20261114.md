# TICKET-3D-TEXT-MIRRORED — 3D text quads render horizontally mirrored

**Stato:** CLOSED (2026-11-14)
**Priorità:** P1
**Ambito:** ChrononTemplate 03 fixture / projected text orientation
**Origine:** `03_rack_focus_3d` frame 30

## Esito

Il mirror non era un errore di UV del text renderer. La camera del fixture
guarda la **faccia posteriore** della card 3D: un testo mappato su un piano
proiettato dal lato posteriore è specchiato per costruzione. La camera 0°
rende il frame nero (il testo non è più in vista), quindi la camera 180° è
quella che inquadra la card.

## Fix

Il fixture 03 applica `scale: [-1, 1, 1]` ai dieci layer testo, orientando la
faccia leggibile verso la camera. Il DoF, la rack-focus e le quote Z restano
invariati. Il testo si legge ora `FOCUS PLANE 3`…`10` senza specchiatura.

## Verifica

- Frame 30 Vulkan: testo leggibile.
- Suite motion-design: **401 checks, 0 failures**.
- Nessuna modifica al text renderer: il comportamento dal retro della card è
  corretto; il fixture non deve inquadrarlo dal retro.

## Nota di prodotto

Se in futuro serve un testo 3D sempre leggibile da entrambi i lati, va aggiunta
una modalità esplicita “double-sided readable text” con flip UV, non un flip
silenzioso del renderer. Non è richiesto per questa consegna.
