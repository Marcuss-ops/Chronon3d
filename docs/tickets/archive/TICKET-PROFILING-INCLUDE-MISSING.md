# TICKET-PROFILING-INCLUDE-MISSING — text_render_resources missing #include

## Stato: DONE-ON-THIS-BRANCH (PARTIAL macchina-verifica, DEFERRED-WBH per AGENTS.md §rot-class-protection threshold)

## Origine (cronaca estesa)
Surfaced during rot-cascade iteration alongside TICKET-FMT-PATH-JOIN-INCOMPLETE-TYPE.
`src/backends/text/text_render_resources.cpp` lines 721-722 referenced
`profiling::g_current_counters` without including `<chronon3d/core/profiling/profiling.hpp>`
— the extern symbol declaration was missing in this TU.

## Soluzione accettabile
Added `#include <chronon3d/core/profiling/profiling.hpp>` between
`#include <chronon3d/cache/lru_cache.hpp>` and
`#include <chronon3d/render_graph/core/render_graph_hashing.hpp>` (alphabetical,
Cat-3 minimal-surface 1-line addition).

## Cascade envelope
With rot profiling #include fix + rot #8 fix in this atomic chore, the
chronon3d_cli target COMPILED verde on this VPS. Broader 11/11 verde
DEFERRED-WBH due to 14+ orthogonal pre-existing rot-classes that halted the
cascade (AGENTS.md §rot-class-protection threshold >2 distinct rot-classes).

## Cross-references
- TICKET-FMT-PATH-JOIN-INCOMPLETE-TYPE (sibling ticket, same atomic chore).
- `docs/baselines/main-df1e09d9-rot-cascade-baseline.md` (rot-class taxonomy).
- AGENTS.md §rot-class-protection threshold.
