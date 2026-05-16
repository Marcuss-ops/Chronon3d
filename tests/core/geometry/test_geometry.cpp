#include <doctest/doctest.h>
#include <chronon3d/geometry/bounds.hpp>
#include <chronon3d/geometry/ray.hpp>
#include <chronon3d/geometry/mesh.hpp>

using namespace chronon3d;

TEST_CASE("AABB operations") {
    AABB box(Vec3(0.0f), Vec3(1.0f));

    SUBCASE("Contains") {
        CHECK(box.contains(Vec3(0.5f)));
        CHECK(box.contains(Vec3(0.0f)));
        CHECK(box.contains(Vec3(1.0f)));
        CHECK_FALSE(box.contains(Vec3(1.1f)));
    }

    SUBCASE("Merge point") {
        box.merge(Vec3(2.0f, 0.5f, 0.5f));
        CHECK(box.max.x == 2.0f);
        CHECK(box.min.x == 0.0f);
    }

    SUBCASE("Intersects") {
        AABB other(Vec3(0.5f), Vec3(1.5f));
        CHECK(box.intersects(other));
        
        AABB separate(Vec3(2.0f), Vec3(3.0f));
        CHECK_FALSE(box.intersects(separate));
    }
}

TEST_CASE("Ray operations") {
    Ray ray(Vec3(0.0f), Vec3(1.0f, 0.0f, 0.0f));

    SUBCASE("Point at t") {
        Vec3 p = ray.point_at(5.0f);
        CHECK(p.x == 5.0f);
        CHECK(p.y == 0.0f);
        CHECK(p.z == 0.0f);
    }
}

TEST_CASE("Mesh operations") {
    Mesh mesh("TestMesh");
    
    mesh.add_vertex(Vertex(Vec3(0.0f, 0.0f, 0.0f)));
    mesh.add_vertex(Vertex(Vec3(1.0f, 1.0f, 1.0f)));
    mesh.add_index(0);
    mesh.add_index(1);

    SUBCASE("Integrity") {
        CHECK(mesh.vertices().size() == 2);
        CHECK(mesh.indices().size() == 2);
        CHECK(mesh.name() == "TestMesh");
    }

    SUBCASE("Automatic bounds") {
        const AABB& b = mesh.bounds();
        CHECK(b.min.x == 0.0f);
        CHECK(b.min.y == 0.0f);
        CHECK(b.min.z == 0.0f);
        CHECK(b.max.x == 1.0f);
        CHECK(b.max.y == 1.0f);
        CHECK(b.max.z == 1.0f);
    }
}
