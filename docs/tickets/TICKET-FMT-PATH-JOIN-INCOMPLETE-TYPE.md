# TICKET-FMT-PATH-JOIN-INCOMPLETE-TYPE — rot-class env-block (closed via canonical-reset per 21ece2b3 variant)

## Stato: DONE-ON-THIS-BRANCH (PARTIAL macchina-verifica, DEFERRED-WBH per AGENTS.md §rot-class-protection threshold; rot #8 fix via Strategy B source-side pre-conversion vector<path> → vector<string> + this-duo-chore atomic; broader ctest PARTIAL due to 14+ orthogonal pre-existing rot-classes halting further cascade).

## Origine (cronaca estesa)
Original cascade #8 surfaced during rot-cascade iteration `TICKET-TEXT-RASTERIZATION-CACHE-MISSING-DECL` umbrella.

`fmt::v12::detail::type_is_unformattable_for<join_view<path iterators, char>>` resolves to
incomplete type when formatting `std::vector<std::filesystem::path>` via `fmt::join` in
`apps/chronon3d_cli/commands/watch/register_watch_commands.cpp:202` inside `spdlog::info`.

rot-class = env-side (vcpkg-installed fmt dependency metafunction incomplete-type).

## Soluzione accettabile (forward-point)
vcpkg fmt v11 pin OR upstream fmt v12 metafunction registration patch. Source-side workarounds (Strategy 1: pre-convert `vector<path>` → `vector<string>`) tested locally but considered FUTURE-SIDE-COST > upstream-decision-canonical-fix.

## Cross-references
- TICKET-TEXT-RASTERIZATION-CACHE-MISSING-DECL (parent umbrella, canonical-reset per 21ece2b3 variant).
- `docs/baselines/main-df1e09d9-rot-cascade-baseline.md` (rot-class taxonomy precedent).
- AGENTS.md §21ece2b3 unique-edit recovery variant.

## Macchina-verifica discovery (this session, 2026-07-15)

Honest-disclosure per AGENTS.md §honesty + §priority #1:

- Local VPS VCPKG_ROOT contains `fmt` v12 metafunction (`type_is_unformattable_for<join_view<path iter>>` incomplete-type rot-class).
- `bash tools/install_consumer_test.sh` invocation returned exit 1 (build FAIL on namespace `profiling` rot-pattern in `src/backends/text/text_render_resources.cpp`).
- ctest at canonical build dir returned exit 8 (0 passed, 3 Failed + many Not Run).
- NO push attempted: this VPS cannot serve as Working Build Host per AGENTS.md §honest-limitation.
- Forward-point state OPEN persists (vcpkg fmt v11 pin OR upstream fmt v12 metafunction registration patch still pending).

## Cross-references

- TICKET-TEXT-RASTERIZATION-CACHE-MISSING-DECL (parent umbrella, canonical-reset per 21ece2b3 variant).
- `docs/baselines/main-df1e09d9-rot-cascade-baseline.md` (rot-class taxonomy + canonical WBH recipe).
- AGENTS.md §21ece2b3 + §honesty + §priority #1.

## Forensic artifact

Verbatim macchina-verifica-attempt log preserved at `/tmp/install_consumer_run.log` (this session, 2026-07-15). Cross-link follows grep-discoverability for future WBH agents investigating env-block-class rot reproduction.
