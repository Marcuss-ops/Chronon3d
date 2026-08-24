# TICKET-SVGPP-SPIKE

## Scope

Isolated path-only importer spike using SVG++ `v1.3.1`, with a Chronon `PathShape` adapter. The existing `svg_path_loader.cpp` remains canonical and unchanged.

## Measurements

| Metric | Result | Status |
|---|---:|---|
| Canonical adapter implementation | 146 LoC (`PathAdapter`) | PASS — internal, no public API expansion |
| Spike runner + corpus | 58 LoC | INFO |
| SVG++ parser TU | 4 LoC | PASS |
| Spike CMake | 20 LoC | PASS |
| SVG++ headers | 192 files / 806,182 bytes | INFO |
| Boost headers | 70,211,577 bytes | INFO |
| vcpkg packages installed for Boost Spirit probe | 63 total package entries | INFO — transitive graph, mostly header-only modules |
| Spike translation-unit rebuild | 18.13 s (`-j8`, existing project build) | PASS |
| Incremental no-op build | 0.16 s | PASS |
| Spike binary file size | 17,332,288 bytes | INFO |
| Spike ELF sections (`size`) | text 488,286; data 2,112; bss 2,000 | INFO |
| Runtime shared-library additions | none beyond libstdc++, libgcc, libc, libm | PASS |

## Corpus coverage

The corpus contains 10 path-data cases:

- absolute/relative `M`, `L`, `H`, `V`, `Z`
- implicit line segments after `M`
- cubic and smooth cubic `C`, `S`
- quadratic and smooth quadratic `Q`, `T`
- compact scientific notation and comma separators
- absolute and relative elliptical arcs `A`, `a`
- repeated mixed curve commands
- malformed numeric input

Observed result:

- SVG++ parser/importer: **10/10 expected outcomes PASS**; malformed input rejected as expected.
- Canonical Chronon adapter: **10/10 PASS**.
- Two arc cases are parsed and converted to cubic Bézier commands before entering `PathShape`.
- Relative-command rejection policy remains covered.
- No SVG XML/document traversal was added; the existing `<path d="...">` extraction remains a deliberately separate boundary.

## Promotion follow-up

The feasibility result was promoted into the canonical asset path:

- `src/assets/svg_importer.cpp` owns the SVG++ adapter and arc conversion.
- `src/assets/svgpp_parser_impl.cpp` owns the explicit SVG++ template instantiation.
- `src/assets/svg_path_loader.cpp` retains only file I/O and the existing narrow `<path d="...">` extraction.
- `tests/assets/test_svg_path_loader.cpp` now locks arc conversion and relative-command policy.
- `tests/assets/CMakeLists.txt` registers a focused loader suite.

## Dependency decision

`svgpp` has no port in the repository's vcpkg catalog. The manifest now includes `boost-spirit` (which pulls the Boost Spirit header graph, 63 installed package entries in the isolated probe), while the spike fetches SVG++ `v1.3.1` through an OFF-by-default `CHRONON3D_BUILD_SVGPP_SPIKE` option. SVG++ is header-only and requires Boost; its documentation states that it has no link library, but the Boost.Spirit template graph is substantial.

## Decision

**PASS for path-data replacement; PARTIAL for full SVG import.** SVG++ is now the canonical parser for path data, including `A/a` via SVG++ arc-to-cubic conversion. The file boundary now uses Boost.PropertyTree XML traversal to locate the first `<path>` `d` attribute; custom string scanning has been removed. Transforms, styles, basic shapes, and a larger corpus remain outside this path-only API and must be handled before claiming complete SVG document coverage.
