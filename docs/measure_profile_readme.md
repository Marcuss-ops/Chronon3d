# Profile Measurement (`tools/measure_profile.sh`)

> Strumento di misurazione canonica dei profili `linux-profile-*`
> del piano 08. Produce un JSON + Markdown riutilizzabili in CI per
> monitorare la crescita di tempi / dimensioni / dipendenze ad ogni
> cambio di profilo o di vcpkg.

## Profili

I quattro profili canonici mappati 1:1 sulle feature di
[`vcpkg.json`](../vcpkg.json) e sui `cmake --preset`:

| Profilo | CMake preset | Feature vcpkg | CMake flags principali |
|---|---|---|---|
| `core` | `linux-profile-core` | `tests` | testo OFF, blend2d OFF, exr OFF, video OFF, mesh OFF, telemetry OFF |
| `motion` | `linux-profile-motion` | `cli;blend2d;text;tests` | testo ON, blend2d ON (grafica tipografica 2D) |
| `video` | `linux-profile-video` | `tests;telemetry;cli;text` | native-ffmpeg ON, telemetry ON |
| `extended` | `linux-profile-extended` | `cli;blend2d;text;mesh;exr;telemetry;tests;benchmarks;profiling` | tutto attivo, unity build OFF |

## Usage

```bash
# Solo misura del configure (più leggero, utile in loop di check):
tools/measure_profile.sh core

# Misura configure + build (richiede toolchain completa):
tools/measure_profile.sh extended --build --parallel 16

# Path report custom (default: build/profiles/<p>/profile-measurement.md):
tools/measure_profile.sh motion --build --report-md /tmp/motion-report.md
```

## Output

* `build/profiles/<name>/profile-measurement.json`
* `build/profiles/<name>/profile-measurement.md`
* Log completo del configure: `build/chronon/<preset>/profile-configure.log`
* Log completo del build (se `--build`): `build/chronon/<preset>/profile-build.log`

Il JSON è la fonte canonica per le pipeline di trend (la
dimensione di `chronon3d_sdk_impl.a` viene monitorata come proxy di
"fabbrica" del SDK). Il Markdown serve come allegato in
`docs/CHANGELOG.md` ad ogni rilascio di profilo.

## CI matrix

La CI `linux-ci` estende la matrice con un job per profilo:

```yaml
profile-core:
  - tools/measure_profile.sh core
profile-motion:
  - tools/measure_profile.sh motion
profile-video:
  - tools/measure_profile.sh video
profile-extended:
  - tools/measure_profile.sh extended --build --parallel 16
```

Solo `extended` esegue il `--build` (sono gli unici target che la
CI `linux-ci` testa in full-validation). I profili `core/motion/video`
misurano solo configure e vcpkg deps per essere leggeri.

## Manutenzione

* Quando un nuovo profilo viene aggiunto in `CMakePresets.json`
  aggiungere la case in `tools/measure_profile.sh`.
* Quando `vcpkg/CMakePresets.json` cambia (gate R4 di `check_doc_sync.sh`)
  rilanciare la misurazione su tutti i profili e committare
  `build/profiles/**` aggiornati (snapshot, ignorati da `.gitignore`
  per non appesantire il diff).
