# Chronon3D — Feature Reference

> Snapshot: `main@25049b2`, 23 giugno 2026.
>
> Stato presente: [`CURRENT_STATUS.md`](CURRENT_STATUS.md).
> Requisiti di release: [`RELEASE_GATE.md`](RELEASE_GATE.md).

La presenza di codice non implica che l’intero sottosistema sia verificato o
release-ready. Questa pagina separa feature presenti, parziali e pianificate.

## Rendering e compositing

### Presenti

- Scene, layer e gerarchie.
- Render graph compilato.
- Backend software CPU-first.
- Immagini, shape e SVG Path V1 (`M/L/H/V/C/Q/Z`, incluse forme relative).
- Mask, blend mode ed effetti software.
- Cache, dirty tracking, frame/history state e telemetria opzionale.
- Output immagine e video controllato dalle opzioni di build.
- Extension modules e registrazione esplicita delle composizioni.

### Parziali

- Precomp annidato, execution scope e concorrenza.
- Determinismo completo tra scheduler/profili.
- Import SVG limitato: primo path, styling/gruppi/trasformazioni/filtri incompleti.
- V3 tile-first non ancora runtime produttivo.

## Testo

### Presenti

- FreeType, HarfBuzz e FriBidi.
- Shaping, bidi, layout, wrapping, overflow e auto-fit.
- `TextSpec`, `TextRunSpec` e `TextDocument`.
- Span di stile e paragrafi.
- Gradient fill, stroke, shadow, material e glyph cache.
- Animator per glifo:
  - position;
  - scale;
  - rotation;
  - skew;
  - anchor;
  - opacity;
  - blur;
  - fill/stroke color;
  - stroke width;
  - tracking;
  - baseline shift;
  - character offset.
- Selector per glyph, grapheme, character, word e line.
- Range shape Square, RampUp, RampDown, Triangle, Round e Smooth.
- Ordine Forward, Reverse, FromCenter, ToCenter e Random deterministico.
- Sample-time sub-frame.
- Text-on-path esistente da consolidare.
- Registry canonico dei preset e sentinel test iniziali.

### Parziali

- Rich text end-to-end con più font/dimensioni/materiali nello stesso paragrafo.
- Animator e identità semantica per span/parola.
- Preset kinetic typography non ancora tutti verificati come prodotto.
- Motion blur testuale e visual regression completa.
- Segmentazione e line breaking globale senza ICU.

### Pianificate

- Timed text/SRT/JSON e word timing produttivo.
- Highlight, karaoke e subtitle layout policies.
- Wiggly, Wave e Random selector completi.
- Variable fonts.
- ICU opzionale.
- Color emoji/fallback completo.
- Expression Selector.
- Text 3D, MSDF, morph e displacement avanzati.

### Limiti noti

- line breaking CJK non ancora basato su ICU;
- color emoji non supportate in modo produttivo;
- Text 3D e MSDF non fanno parte del profilo stabile.

## Camera

### Presenti

- `Camera2_5D` come snapshot runtime.
- `CameraDescriptor` come authoring canonico futuro.
- `compile_camera()` e `CameraProgram` immutabile.
- `CameraSession` per stato per-job.
- Zoom, FOV e Physical Lens projection.
- Focal length, sensor size, gate fit, pixel aspect e anamorphic contract.
- Focus distance, aperture, temporal motion blur e depth-of-field di base.
- Pose Tracks, Orbit Motion e Trajectory Motion.
- Look-at point/layer e tipo OrientAlongPath.
- Idle oscillation e handheld noise deterministico.
- Constraint tipizzati.
- Fingerprint del descriptor.
- Shot timeline.
- Transizioni Cut, Smooth Blend, Push, Whip Pan e Focus Handoff.
- Sub-frame sampling.
- Checkpoint/pre-roll per accesso non sequenziale.
- Camera debug/validation foundations.

### Parziali

- `OrientAlongPath` completo e banking.
- Framing solver bounds-aware, multi-target e rule-of-thirds.
- Near/far clipping di primitive complesse.
- DOF fisico con Circle of Confusion e near/far separation.
- Diagnostica della shot timeline.
- Preset e adapter legacy ancora sovrapposti al percorso compilato.
- Alcuni fix camera sono compilati ma test-blocked da TICKET-029.

### Pianificate

- Rimozione dei percorsi authoring legacy.
- CLI camera validate/path-report/debug-video stabile.
- Lens effects avanzati e bokeh fisico più completo.
- Multi-camera avanzata, se richiesta dal prodotto dopo Camera Production V1.

## SDK

### Presenti

- Header pubblici installabili.
- Archivio statico aggregato `libchronon3d_sdk_impl.a`.
- `Chronon3DConfig.cmake`, version file e targets export.
- Target pubblico `Chronon3D::SDK`.
- Registry CMake centrale.
- Consumer esterno `find_package` per il confine install/link.
- `ExtensionModule` e `ExtensionContext` per pack C++ esterni.

### Parziali

- Consumer corrente verifica package e simbolo, non un render completo.
- Documentazione pubblica e compatibility policy non ancora da release.
- Release artifact Linux non ancora certificati sullo stesso commit.

### Pianificate

- Consumer end-to-end che renderizza testo, camera ed effetti.
- Formato dichiarativo versionato `.chronon`.
- C ABI stabile con handle opachi.
- Binding Python iniziale e binding successivi sullo stesso ABI.
- Plugin binari versionati per pack C++ complessi.

## Expressions V2 — quarantena sperimentale

Expressions V2 è presente sotto `experimental/expressions/`, ma non è una
feature pubblica stabile.

| Superficie | Stato reale |
|---|---|
| Root | `experimental/expressions/` |
| Build switch | `CHRONON3D_BUILD_EXPERIMENTAL=ON` |
| Default | Escluso |
| Install/export SDK | No |
| Integrazione produttiva | No |

Non includere header sperimentali nel codice stabile prima della promozione e
della rimozione approvata della quarantena.
