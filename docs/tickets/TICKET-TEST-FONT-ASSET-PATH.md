# TICKET-TEST-FONT-ASSET-PATH

**Status**: WIRED (P1, macchina-verifica pending)
**Forward-point from**: `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` forward-point (g)
**Discovered via**: `chronon3d_content_tests --test-case='*ShapedGlyphLine*'`

## Problema osservato

Il test harness non trovava `assets/fonts/Poppins-Regular.ttf` nel proprio asset root. Il risultato osservato era 1/8 test ShapedGlyphLine passato e 7/8 falliti con shaping HarfBuzz a zero glifi.

Il difetto è un **ASSET-BLOCK**, non un errore del decoder o del renderer.

## Soluzione implementata

È stata scelta la variante CI-only, senza tracciare un secondo binario font nel repository:

- `tools/bootstrap_test_fonts.py` scarica gli oggetti blob ufficiali dal repository `google/fonts`;
- il payload viene verificato ricostruendo il Git object checksum prima della scrittura;
- la scrittura è atomica;
- il download avviene soltanto quando `CHRONON3D_BOOTSTRAP_TEST_FONTS=ON`;
- le build locali normali rimangono network-free;
- i preset `linux-ci` e `linux-ci-full-validation` abilitano il bootstrap;
- `chronon3d_content_tests` dipende dal target bootstrap soltanto nei preset che lo richiedono.

Oggetti pinned:

| Fixture | Git blob SHA |
|---|---|
| `Poppins-Regular.ttf` | `0bda228ade88b0bb5aac7da2c881d0c3f64d0817` |
| `Poppins-OFL.txt` | `76df3b565672e3248a5715339d092d4cb6c75019` |

Destinazione build-relative:

```text
${CMAKE_BINARY_DIR}/assets/fonts/Poppins-Regular.ttf
${CMAKE_BINARY_DIR}/assets/fonts/Poppins-OFL.txt
```

Non vengono introdotti path assoluti nel test binary e non viene usata la working directory come resolver alternativo.

## Criteri di accettazione

1. Il bootstrap produce il font e la licenza nella directory asset del build.
2. Il checksum Git dei file prodotti coincide con i blob pinned.
3. I test ShapedGlyphLine risolvono il font tramite il normale asset root del test.
4. I 7 casi precedentemente falliti risultano verdi.
5. `tools/check_text_functional_linux.sh` completa il gate.
6. CI/WSL/VPS non dipendono da un path assoluto del checkout.

## Forward-points

| Forward-point | Stato | Chiusura |
|---|---|---|
| `PHASE-1-FONT-ASSET-PATH-AUDIT` | PARTIAL | Inventario completo dei consumer Poppins ancora da osservare sulla macchina di verifica. |
| `PHASE-2-FONT-ASSET-INSTALL` | WIRED | Bootstrap, checksum e CMake dependency implementati; chiude dopo test ShapedGlyphLine osservati verdi. |
| `PHASE-3-CI-PORTABILITY` | WIRED | Preset CI e path build-relative implementati; chiude dopo una run CI/WSL/VPS osservata. |

## Verifica ancora richiesta

Comandi canonici:

```bash
cmake --preset linux-ci
cmake --build --preset linux-ci --target chronon3d_content_tests
./build/chronon/linux-ci/tests/chronon3d_content_tests \
  --test-case='*ShapedGlyphLine*'
```

Lo stato resta **WIRED**, non **PASS**, finché i sette test e il gate testuale non vengono osservati verdi su una macchina di verifica.

## Limitazione correlata

Il precedente SIGABRT in `test_shaped_glyph_line_cluster_benchmark.cpp` rimane un problema separato e non deve essere nascosto dalla disponibilità del font.
