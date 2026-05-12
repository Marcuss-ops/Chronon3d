# Chronon3d

Chronon3d is a modular C++20 3D engine/library focused on geometry, scene management, transforms, and backend-independent rendering.

## Goals

- Clean C++20 architecture
- Modular 3D math and geometry system
- Scene graph
- Testable rendering pipeline
- CLI/examples-first development

## Non-goals

- Not a Blender clone
- Not a full game engine at the beginning
- Not GUI-first
- Not dependent on one rendering backend

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## Roadmap

* [ ] Math module (Vec, Mat, Quat, Transform)
* [ ] Geometry module (Mesh, Vertex, Primitives)
* [ ] Scene graph (Node, Scene, Camera)
* [ ] CLI Tool
* [ ] Software renderer
* [ ] OpenGL/Vulkan backend
