# TICKET — Demolition Debt: MeshShape::mesh Compatibility Slot

This ticket records the demolition debt for the legacy in-memory mesh slot in `MeshShape`.

## Metadata

- **Owner:** Scene model / Assets migration (`renderer::ShapeProcessor`)
- **Reason:** `MeshShape` retains an in-memory `std::shared_ptr<Mesh> mesh` compatibility escape hatch alongside `assets::MeshRef reference` and `assets::PreparedMeshSourceRef prepared`. `software_mesh_processor.cpp:21-22` resolves meshes via `source || legacy_mesh`. Authoring never populates `mesh`, but in-memory test fixtures and legacy bridges still access it directly instead of registering assets through `AssetResolver`.
- **Exit condition:** All mesh production and test paths construct meshes via `PreparedMeshSourceRef` / `AssetResolver`; zero consumers pass or inspect `MeshShape::mesh`.
- **Equivalence test/gate:** `tests/backends/test_software_backend.cpp` mesh rendering tests and `tests/scene/test_scene.cpp` verified identical rasterization via prepared mesh sources.
- **Removal scope:**
  1. `MeshShape::mesh` member in `include/chronon3d/scene/model/shape/shape.hpp`.
  2. Fallback check `if (!source && shape.mesh)` in `src/backends/software/software_mesh_processor.cpp`.
  3. Direct assignments to `.mesh` in legacy test fixtures.
- **Status:** `ACTIVE`
