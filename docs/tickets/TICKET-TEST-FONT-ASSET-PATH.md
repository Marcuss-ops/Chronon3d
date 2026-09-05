# TICKET-TEST-FONT-ASSET-PATH

**Status**: WIRED (P1, macchina-verifica pending)

## 2026-09-05 — fixture fleet re-pinned (bootstrap v2)

- `tools/bootstrap_test_fonts.py` extended to all 21 pinned fixtures: 9 heavy
  fonts (original fleet) + 7 UI fonts (Inter/Poppins/DMSans/Georgia, demolished
  from main in `3bed1ea0e`) + 5 images (checker/grid_tile/landscape/camera_ref/test_image).
- Resolution order: local git object store (`git cat-file`, network-free,
  snapshot `a31b1627`) → pinned GitHub raw fallback. Every payload verified by
  git blob SHA-1 before atomic write.
- `.gitignore`: `assets/` now ignored wholesale (bootstrap-only policy); the
  old `!assets/**` negation was un-tracking the fixtures by accident.
- Observed: `chronon3d_text_fallback_tests` PASS; `chronon3d_render_graph_tests`
  371/373 (residual = in-flight CompiledResourcePlan/pinned-anchor work, not
  asset debt); ShapedGlyphLine content-suite observation still pending (WIRED
  criterion unchanged).
**Forward-point from**: `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` forward-point (g)
**Discovered via**: `chronon3d_content_tests --test-case='*ShapedGlyphLine*'`

## Problema osservato

Il test harness non trovava `assets/fonts/Poppins-Regular.ttf`. Il risultato osservato era 1/8 test ShapedGlyphLine passato e 7/8 falliti con shaping HarfBuzz a zero glifi.

Il difetto è un **ASSET-BLOCK**, non un errore del decoder o del renderer.

## Soluzione implementata

È stata scelta la variante CI-only, senza tracciare un secondo binario font nel repository:

- `tools/bootstrap_test_fonts.py` scarica gli oggetti blob ufficiali dal repository `google/fonts`;
- il payload viene verificato ricostruendo il Git object checksum prima della scrittura;
- la scrittura è atomica;
- il download avviene soltanto quando `CHRONON3D_BOOTSTRAP_TEST_FONTS=ON`;
- le build locali normali rimangono network-free;
- i preset `linux-ci` e `linux-ci-full-validation` abilitano il bootstrap;
- `chronon3d_content_tests` dipende dal bootstrap soltanto nei preset che lo richiedono.

Oggetti pinned:

| Fixture | Git blob SHA |
|---|---|
| `Poppins-Regular.ttf` | `0bda228ade88b0bb5aac7da2c881d0c3f64d0817` |
| `Poppins-OFL.txt` | `76df3b565672e3248a5715339d092d4cb6c75019` |

Il helper canonico registra i test con `WORKING_DIRECTORY=${CMAKE_SOURCE_DIR}`. Per rendere visibile il path letterale già usato dai test, il bootstrap esplicito installa nella checkout temporanea:

```text
${CMAKE_SOURCE_DIR}/assets/fonts/Poppins-Regular.ttf
${CMAKE_SOURCE_DIR}/assets/fonts/Poppins-OFL.txt
```

Questo non è un fallback CWD del runtime: è una fixture test-only materializzata nel path dichiarato dal test. Non viene inserito alcun path assoluto nel test binary.

## Criteri di accettazione

1. Il bootstrap produce font e licenza nella checkout di verifica.
2. Il checksum Git dei file prodotti coincide con i blob pinned.
3. I test ShapedGlyphLine trovano `assets/fonts/Poppins-Regular.ttf` dal working directory canonico.
4. I 7 casi precedentemente falliti risultano verdi.
5. `tools/check_text_functional_linux.sh` completa il gate.
6. CI/WSL/VPS non dipendono da un path assoluto del checkout.

## Forward-points

| Forward-point | Stato | Chiusura |
|---|---|---|
| `PHASE-1-FONT-ASSET-PATH-AUDIT` | PARTIAL | Inventario completo dei consumer Poppins ancora da osservare sulla macchina di verifica. |
| `PHASE-2-FONT-ASSET-INSTALL` | WIRED | Bootstrap, checksum e CMake dependency implementati; chiude dopo test ShapedGlyphLine osservati verdi. |
| `PHASE-3-CI-PORTABILITY` | WIRED | Preset CI e path source-relative portabile implementati; chiude dopo una run CI/WSL/VPS osservata. |

## Verifica ancora richiesta

```bash
cmake --preset linux-ci
cmake --build --preset linux-ci --target chronon3d_content_tests
./build/chronon/linux-ci/tests/chronon3d_content_tests \
  --test-case='*ShapedGlyphLine*'
```

Lo stato resta **WIRED**, non **PASS**, finché i sette test e il gate testuale non vengono osservati verdi.

## Limitazione correlata

Il precedente SIGABRT in `test_shaped_glyph_line_cluster_benchmark.cpp` rimane un problema separato e non deve essere nascosto dalla disponibilità del font.
