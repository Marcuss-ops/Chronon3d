# TICKET-FMT-PATH-JOIN-INCOMPLETE-TYPE — rot-class env-block (closed via canonical-reset per 21ece2b3 variant)

## Stato: DONE-ON-UPSTREAM-PATH via AGENTS.md §21ece2b3 unique-edit-recovery variant (POST-1st-REBASE 2026-07-15: rebase onto 546e5b4f dropped local c4ccd1b1 std::transform rot#8 fix in favor of upstream 9121edfa for-loop equivalent — `git checkout --theirs` chose upstream's clean implementation; PARTIAL macchina-verifica, DEFERRED-WBH per AGENTS.md §rot-class-protection threshold; rot #8 fix is upstream's source-side pre-conversion vector<path> → vector<string> at apps/chronon3d_cli/commands/watch/register_watch_commands.cpp:211-216; broader 11/11 macchina-verifica DEFERRED-WBH — re-verified chronon3d_cli-compiles-cleanly on this VPS post-rebase; rot profiling #include fix at src/backends/text/text_render_resources.cpp:8 preserved auto-merge clean).

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
- TICKET-PROFILING-INCLUDE-MISSING (sibling rot-class: profiling #include; same atomic 2-file chore as rot #8).

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
