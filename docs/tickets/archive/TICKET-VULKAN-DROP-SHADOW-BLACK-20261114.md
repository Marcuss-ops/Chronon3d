# TICKET-VULKAN-DROP-SHADOW-BLACK — drop_shadow on a screen-space shape blacks out the Vulkan frame

**Stato:** CLOSED (2026-11-17) — FIXED 2026-11-14; test Vulkan strict 1/1 con 522/522 asserzioni e piano 12 con 13/13 frame non neri; nessun workaround.
**Priorità:** P1
**Ambito:** Chronon3d Vulkan backend / native effect-stack drop shadow
**Origine:** ChrononTemplate motion-design stress, case 12 (`12_overshoot`)

## Causa

Il native drop-shadow accettava solo `radius <= 32`; il piano 12 usa `36`.
Nello strict stack Vulkan lo stage non veniva gestito, la source surface veniva
rilasciata e il fallback software non è ammesso: il frame restava nero.

## Fix engine

`Chronon3d/src/render_graph/nodes/effect_stack_node_native_detail.cpp`:
finestra nativa del drop shadow estesa da **32 a 64** (il blur ambientale
arriva a 128 px). Il piano 12 conserva il raggio 36 e il suo shadow.

Il test di parità è aggiornato: il caso fuori finestra ora è `radius=64.1`
(atteso fail-closed), non più `32.1`.

## Verifica

- Test Vulkan strict DropShadow: **1/1 test, 522/522 assertion**.
- Piano 12 Vulkan: **13/13 frame non neri**; media frame da 3.34 (solo backdrop
  al frame 0) a **20.21** con badge/testo visibili.
- Nessun workaround: il piano non è stato modificato e il software fallback non
  è usato per 12.
