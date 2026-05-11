#pragma once

#include <filament/Engine.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <math/vec3.h>
#include <math/vec2.h>
#include <vector>
#include <array>

namespace graphis3d {

struct Vertex {
    filament::math::float3 position;
    filament::math::float3 normal;
    filament::math::float2 uv;
};

class GeometryProvider {
public:
    static void createCube(filament::Engine& engine, 
                          filament::VertexBuffer** outVb, 
                          filament::IndexBuffer** outIb) {
        
        static const Vertex vertices[] = {
            // Front
            {{-0.5, -0.5,  0.5}, {0, 0, 1}, {0, 0}}, {{ 0.5, -0.5,  0.5}, {0, 0, 1}, {1, 0}},
            {{ 0.5,  0.5,  0.5}, {0, 0, 1}, {1, 1}}, {{-0.5,  0.5,  0.5}, {0, 0, 1}, {0, 1}},
            // Back
            {{-0.5, -0.5, -0.5}, {0, 0, -1}, {1, 0}}, {{-0.5,  0.5, -0.5}, {0, 0, -1}, {1, 1}},
            {{ 0.5,  0.5, -0.5}, {0, 0, -1}, {0, 1}}, {{ 0.5, -0.5, -0.5}, {0, 0, -1}, {0, 0}},
            // Top
            {{-0.5,  0.5, -0.5}, {0, 1, 0}, {0, 1}}, {{-0.5,  0.5,  0.5}, {0, 1, 0}, {0, 0}},
            {{ 0.5,  0.5,  0.5}, {0, 1, 0}, {1, 0}}, {{ 0.5,  0.5, -0.5}, {0, 1, 0}, {1, 1}},
            // Bottom
            {{-0.5, -0.5, -0.5}, {0, -1, 0}, {0, 0}}, {{ 0.5, -0.5, -0.5}, {0, -1, 0}, {1, 0}},
            {{ 0.5, -0.5,  0.5}, {0, -1, 0}, {1, 1}}, {{-0.5, -0.5,  0.5}, {0, -1, 0}, {0, 1}},
            // Left
            {{-0.5, -0.5, -0.5}, {-1, 0, 0}, {0, 0}}, {{-0.5, -0.5,  0.5}, {-1, 0, 0}, {1, 0}},
            {{-0.5,  0.5,  0.5}, {-1, 0, 0}, {1, 1}}, {{-0.5,  0.5, -0.5}, {-1, 0, 0}, {0, 1}},
            // Right
            {{ 0.5, -0.5, -0.5}, {1, 0, 0}, {1, 0}}, {{ 0.5,  0.5, -0.5}, {1, 0, 0}, {1, 1}},
            {{ 0.5,  0.5,  0.5}, {1, 0, 0}, {0, 1}}, {{ 0.5, -0.5,  0.5}, {1, 0, 0}, {0, 0}},
        };

        static const uint16_t indices[] = {
             0,  1,  2,  2,  3,  0, // Front
             4,  5,  6,  6,  7,  4, // Back
             8,  9, 10, 10, 11,  8, // Top
            12, 13, 14, 14, 15, 12, // Bottom
            16, 17, 18, 18, 19, 16, // Left
            20, 21, 22, 22, 23, 20  // Right
        };

        *outVb = filament::VertexBuffer::Builder()
            .vertexCount(24)
            .bufferCount(1)
            .attribute(filament::VertexAttribute::POSITION, 0, filament::VertexBuffer::AttributeType::FLOAT3, 0, sizeof(Vertex))
            .attribute(filament::VertexAttribute::TANGENTS, 0, filament::VertexBuffer::AttributeType::FLOAT3, offsetof(Vertex, normal), sizeof(Vertex)) // Simulating tangents with normals for now
            .attribute(filament::VertexAttribute::UV0, 0, filament::VertexBuffer::AttributeType::FLOAT2, offsetof(Vertex, uv), sizeof(Vertex))
            .build(engine);

        (*outVb)->setBufferAt(engine, 0, filament::VertexBuffer::BufferDescriptor(vertices, sizeof(vertices)));

        *outIb = filament::IndexBuffer::Builder()
            .indexCount(36)
            .bufferType(filament::IndexBuffer::IndexType::USHORT)
            .build(engine);

        (*outIb)->setBuffer(engine, filament::IndexBuffer::BufferDescriptor(indices, sizeof(indices)));
    }
};

} // namespace graphis3d
