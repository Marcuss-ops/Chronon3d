# TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3

## Stato

**DONE — aliases removed, not deprecated (2026-07-15).**

## Risultato finale

`chronon render` è l'unico comando di rendering pubblico:

```bash
chronon render Hero --frame 42 -o hero.png
chronon render Hero --frames 0-90 -o frames/frame_####.png
chronon render Hero --frames 0-90 -o hero.mp4
```

I subcommand `chronon still` e `chronon video`, inizialmente conservati con TTL e warning di deprecazione, sono stati eliminati fisicamente dopo il completamento del dispatcher `RenderJob` canonico.

## Superficie rimossa

- registrazione CLI `still`;
- registrazione CLI `video`;
- warning e adapter di deprecazione;
- `StillArgs`, `VideoArgs`, `VideoCameraArgs`;
- target `chronon3d_cli_video`;
- macro `CHRONON3D_BUILD_CLI_VIDEO` e `CHRONON3D_HAS_CLI_VIDEO`;
- command group e file sorgente degli alias.

L'export MP4 resta disponibile tramite `chronon render ... -o file.mp4` e usa l'exporter FFmpeg interno del `RenderJob` canonico.

## Anti-regressione

- `tools/check_no_legacy_render_cli.sh` impedisce il ritorno dei simboli e target ritirati;
- `tools/verify_cli_render_surface_linux.sh` verifica sul binario che `still` e `video` non compaiano in `--help`.

## Criteri di accettazione

- [x] `render` supporta still, sequence e video;
- [x] alias rimossi dalla registry;
- [x] file e tipi legacy rimossi;
- [x] nessun executor video parallelo;
- [x] gate anti-regressione bloccante;
- [ ] nuova baseline completa WBH sullo stesso commit.

## Forward point reale

La UX di progetto esterno `cd my-video && chronon render HelloWorld ...` resta tracciata esclusivamente da `TICKET-ADD-LOADER-FOR-CHRONON-JSON`; non richiede il ripristino degli alias.
