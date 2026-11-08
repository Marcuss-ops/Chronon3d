# GEL cinematic title — dielectric revision (2026-11-07)

Review frame 0 of the 1920×1080 GEL title. This is a visual review artifact, not
the final master: human approval is still pending.

## Final frame

- `GEL_Cinematic_Title_1080p_Dielectric.png` — final frame (1920×1080)
- `dielectric_revision.plan.json` — canonical plan emitted from
  `ChrononTemplate` (4 layers: plate + three dielectric faces)
- `dielectric_revision.rgba` — exact RGBA readback used for the PNG
- `dielectric_revision.rgba.timing.json` — per-frame timing sidecar
- `dielectric_revision.log` — strict Vulkan render log

## Drive delivery

- Folder: [Tutti](https://drive.google.com/drive/folders/1ck40GwTSzctOZjzNQzkY0_JvKD11_U6g)
- File: [GEL_Cinematic_Title_1080p_Dielectric_20261107.png](https://drive.google.com/file/d/1PvliLvUN7ZfBn1xuKg-T4n1n8tfGjWWP/view?usp=drivesdk)
- Drive ID: `1PvliLvUN7ZfBn1xuKg-T4n1n8tfGjWWP`
- Uploaded SHA-256: `28677cc14b2a769866a8019b0b83d498d2f9328aef0e7c29e6d6d68aa036beb4`
- Uploaded bytes: `847679`

## Visual decisions

- The plate is one full-frame asset; no strip seam at the old y=415/666 cut.
- `calories` and `ingredients` use the cyan fill `#00E8FF`. `or` is authored
  with `#FFFFFF` so the white slit reads through the transparent body.
- The lime crest is generated from the SDF upper lip, not from a global text
  tint, and has its own edge alpha so it survives the bright slit.
- Light rays are a soft downward accumulation (not a 360° fan). The final pass
  lowers their alpha and widens the horizontal samples so they read as
  volumetric shafts instead of striped columns.
- The earlier extruded backings are not emitted: on the video path they were
  composited over the GEL faces and hid the crest on `ingredients`. The face
  drop shadow now owns the depth cue.

## Verification

- Pack contract: `14 checks, 0 failures`
- Render: `chronon3d_cli render --backend vulkan --gpu-hot-path-mode require_gpu_native`
- `render_ms=320.19`; raw artifact verified at 8,294,400 bytes
- No fallback / software route in the final render log

## SHA-256

```
3dde30888b7e063d0e29ad7dff59b58016be276f14c756c8209c331784db707c  dielectric_revision.plan.json
28677cc14b2a769866a8019b0b83d498d2f9328aef0e7c29e6d6d68aa036beb4  GEL_Cinematic_Title_1080p_Dielectric.png
b9e33fa08c57dec797399ce4e63935a5ab1d102bfc63d00d3de0b994ad6ffbc2  dielectric_revision.rgba
```
