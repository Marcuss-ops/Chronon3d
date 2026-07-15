# TICKET-FMT-PATH-JOIN-INCOMPLETE-TYPE — rot-class env-block (closed via canonical-reset per 21ece2b3 variant)

## Stato: OPEN (canonical-reset per AGENTS.md §21ece2b3 unique-edit recovery variant; 11/11 macchina-verifica DEFERRED-WBH)

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
