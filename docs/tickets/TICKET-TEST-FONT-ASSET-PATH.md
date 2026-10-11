# TICKET-TEST-FONT-ASSET-PATH

**Status**: RESOLVED / SUPERSEDED — pinned asset integrity and the current
relative Poppins path are verified; the original ShapedGlyphLine content suite
was deliberately retired and its exact gate is no longer part of the project.

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

I criteri seguenti descrivono il test harness originale; dopo la rimozione
deliberata della suite esterna, la loro disposizione è registrata nel
re-audit checkout in fondo a questa scheda.

1. Il bootstrap produce font e licenza nella checkout di verifica.
2. Il checksum Git dei file prodotti coincide con i blob pinned.
3. I test ShapedGlyphLine trovano `assets/fonts/Poppins-Regular.ttf` dal working directory canonico.
4. I 7 casi precedentemente falliti risultano verdi.
5. `tools/check_text_functional_linux.sh` completa il gate.
6. CI/WSL/VPS non dipendono da un path assoluto del checkout.

## Forward-points

| Forward-point | Stato | Chiusura |
|---|---|---|
| `PHASE-1-FONT-ASSET-PATH-AUDIT` | SUPERSEDED | La suite che originava il census è stata ritirata; il path Poppins mantenuto è verificato dal test cache e dal CTest source-root contract. |
| `PHASE-2-FONT-ASSET-INSTALL` | RESOLVED | I 21 fixture pinned sono materializzati e verificati; il test relativo Poppins passa 5/5 asserzioni. |
| `PHASE-3-CI-PORTABILITY` | PARTIAL (forward-pointed) | Path source-relative e CTest working directory verificati localmente; clean same-SHA certification resta in `CERT-SEQUENCE-WBH` / `MAIN-TRUTH`. |

## Percorso di verifica storico (retirato)

Questa era la procedura di accettazione quando il ticket fu aperto; il target,
la suite e il gate sono stati successivamente rimossi. La disposizione corrente
è riportata nel re-audit in fondo a questa scheda.

```bash
cmake --preset linux-ci
cmake --build --preset linux-ci --target chronon3d_content_tests
./build/chronon/linux-ci/tests/chronon3d_content_tests \
  --test-case='*ShapedGlyphLine*'
```

Questo comando storico non è più eseguibile nel checkout corrente.

## Limitazione correlata

Il precedente SIGABRT era associato a una suite che è stata poi rimossa; non si
inferisce da quel risultato un difetto nel test corpus mantenuto.

## Re-audit checkout — 2026-10-11

- `python3 tools/bootstrap_test_fonts.py` materialized all 21 pinned fixtures;
  the subsequent `python3 tools/bootstrap_test_fonts.py --check` exited 0.
  `Poppins-Regular.ttf` matches its pinned Git blob SHA. Generated assets remain
  ignored by Git.
- The historical acceptance path is not present in this checkout:
  `chronon3d_content_tests` is not a target in the `linux-fast-dev` build,
  `tools/check_text_functional_linux.sh` is absent, and no
  `ShapedGlyphLine` test source is present under `tests/`. The current `linux-ci`
  preset registers `chronon3d_cli` and `chronon3d_tests`, not the removed
  content target. Git history confirms the three `tests/content/test_shaped_glyph_line*`
  files were explicitly removed in `0e220c6e0` (`chore(tests): remove retired
  external content suite`); restoring them would contradict that retirement.
- Replacement evidence on the maintained path: CTest runs the current fallback
  suite successfully (15/15 cases, 115/115 assertions); the shaping corpus
  passes 13/13 and 116/116; and the Poppins relative-path/cache test passes
  1/1 and 5/5. `chronon3d_add_test_suite()` sets `WORKING_DIRECTORY
  ${CMAKE_SOURCE_DIR}`, so the registered test resolves the relative font path
  from the source root.
- Disposition: close the fixture-path issue as resolved and supersede its exact
  seven-case/gate criteria with the maintained tests above. This does not claim
  clean-SHA CI certification, which remains tracked by the separate WBH/main
  truth tickets. No GPU is required for these font/asset checks.
- `chronon3d_core_tests --test-case='freetype_face_cache: concurrent get_face across many threads'`
  passed 1/1 case and 5/5 assertions from the repository root. That maintained
  test opens the literal relative path `assets/fonts/Poppins-Regular.ttf` and
  skips before assertions when fixtures are missing, so the nonzero assertion
  count verifies this checkout's working-directory path resolves correctly.
- The current shaping corpus (`ShapingCorpus:*`) passed 13/13 cases and
  116/116 assertions. It provides multilingual cluster/advance coverage but
  does not restore the seven retired `ShapedGlyphLine` cases.
