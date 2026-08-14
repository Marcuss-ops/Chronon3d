# ADR-029 — Project semantic content registry

Status: Accepted
Date: 2026-08-14

## Context

External projects that use Chronon3D need a small deterministic container for semantic content that belongs to the project rather than to the renderer: reusable phrases, important words, logical image references, and names paired with text.

The repository rules prohibit adding independent registries, resolvers, caches, service locators, or global singletons without an ADR. They also require new features to reuse a canonical registry/resolver/sampler path and keep the public SDK focused rather than introducing a mega-header.

## Decision

Add one generic, instance-owned `chronon3d::registry::ContentRegistry<Entry>` in the existing canonical registry area. The same implementation is specialized through entry types and aliases for four content families:

- `PhraseEntry` / `PhraseRegistry`
- `ImportantWordEntry` / `ImportantWordRegistry`
- `ImageEntry` / `ImageRegistry`
- `NamedTextEntry` / `NamedTextRegistry`

`ContentRegistrySet` owns one instance of each registry for project-local use.

The registry uses `std::map<std::string, Entry, std::less<>>` so `available()` and `list()` are deterministic by id. It supports add, upsert, lookup, erase, clear, and deterministic snapshots. Empty ids and duplicate `add()` operations fail explicitly.

The implementation is header-only. No new CMake library target, singleton, global registry, resolver, cache, filesystem owner, or background service is introduced.

`ImageEntry::asset_path` is only a logical project asset path/id. Filesystem resolution remains owned by the existing engine-local `AssetResolver`; the semantic registry performs no I/O.

The focused public include is:

```cpp
#include <chronon3d/registry/content_registry.hpp>
```

External C++ projects continue to link only the existing public target:

```cmake
find_package(Chronon3D CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Chronon3D::SDK)
```

No `chronon3d.hpp` or other umbrella header is introduced.

## API justification

These symbols are intentionally public because the requested use case crosses repository boundaries: another project must be able to populate and query the semantic registries without depending on Chronon3D internals or the CLI.

The surface is kept narrow: one generic storage mechanism, four plain data records, four aliases, and one aggregate set. Rendering semantics remain outside this API.

## Consequences

Positive:

- one canonical implementation instead of four duplicated registries;
- deterministic iteration and snapshots;
- no global state and no hidden filesystem behavior;
- usable from installed SDK consumers through `Chronon3D::SDK`;
- easy to extend with another entry type without copying registry logic.

Tradeoffs:

- registry entries are intentionally storage-only and do not resolve images or create text layers;
- JSON/schema serialization is not part of this ADR and can be added later as a separate focused adapter if a cross-language manifest is required.

## Validation

Unit tests cover all four families, duplicate/empty-id rejection, upsert, erase, lookup, and deterministic id ordering. The public-header manifest installs the focused registry header as part of the SDK package.
