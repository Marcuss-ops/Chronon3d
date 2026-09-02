# External content packs

Chronon3d core is an engine, not a content library. Project compositions,
showcases, product-launch demos, certification compositions, experimental
scenes, and other presentation content should live outside the core repository
and be supplied to the engine as external packs.

## Core repository policy

The core repository keeps only engine source, public headers, applications,
build tooling, schemas, tests, and the smallest fixtures required by those
tests. Large demo/content trees and heavyweight multilingual font fixtures are
not bundled with `main`.

Heavy test fonts can be restored on demand with:

```bash
python3 tools/bootstrap_test_fonts.py
```

The bootstrap script is checksum-pinned to the exact fixture blobs that existed
before the cleanup.

## Migrating the previous bundled content

The last `main` snapshot containing the historical `content/` tree is:

```text
a31b162795d95c58e7a4e4d05df83398604487fb
```

Use that snapshot as the source when curating a dedicated external content pack.
Do not copy the complete content tree back into the engine repository. Content
needed for automated core tests should instead be reduced to minimal fixtures
under `tests/`.

This keeps the architectural boundary explicit:

```text
ENGINE != CONTENT LIBRARY
```
