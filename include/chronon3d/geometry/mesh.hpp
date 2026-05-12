#pragma once

#include <chronon3d/geometry/vertex.hpp>
#include <chronon3d/geometry/bounds.hpp>
#include <vector>
#include <string>

namespace chronon3d {

class Mesh {
public:
    Mesh() = default;
    Mesh(std::string name) : m_name(std::move(name)) {}

    void add_vertex(const Vertex& v) {
        m_vertices.push_back(v);
        m_bounds.merge(v.position);
    }

    void add_index(u32 index) {
        m_indices.push_back(index);
    }

    [[nodiscard]] const std::vector<Vertex>& vertices() const { return m_vertices; }
    [[nodiscard]] const std::vector<u32>& indices() const { return m_indices; }
    [[nodiscard]] const AABB& bounds() const { return m_bounds; }
    [[nodiscard]] const std::string& name() const { return m_name; }

    void clear() {
        m_vertices.clear();
        m_indices.clear();
        m_bounds = AABB();
    }

private:
    std::string m_name;
    std::vector<Vertex> m_vertices;
    std::vector<u32> m_indices;
    AABB m_bounds;
};

} // namespace chronon3d
