#include <doctest/doctest.h>
#include <chronon3d/scene/model/transform_resolver.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <cmath>

using namespace chronon3d;

TEST_CASE("Transform3D: local matrix calculations with anchor point") {
    Transform3D t;
    t.position = Vec3(100.0f, 50.0f, 0.0f);
    t.anchor = Vec3(50.0f, 25.0f, 0.0f);
    t.scale = Vec3(2.0f, 2.0f, 1.0f);
    t.rotation = Vec3(0.0f, 0.0f, 90.0f); // 90 degrees around Z axis

    Mat4 mat = t.to_mat4();

    // Verify projection of anchor point (50, 25, 0)
    // local_matrix = Translate(position) * Rotate(rotation) * Scale(scale) * Translate(-anchor)
    // Anchor point relative to local coords should map to position in world coords
    Vec4 p_anchor = mat * Vec4(50.0f, 25.0f, 0.0f, 1.0f);
    CHECK(p_anchor.x == doctest::Approx(100.0f));
    CHECK(p_anchor.y == doctest::Approx(50.0f));
    CHECK(p_anchor.z == doctest::Approx(0.0f));
}

TEST_CASE("TransformResolver3D: simple parenting") {
    std::vector<TransformResolver3D::InputNode> nodes;
    
    Transform3D parent;
    parent.position = Vec3(100.0f, 0.0f, 0.0f);
    
    Transform3D child;
    child.parent_name = "parent";
    child.position = Vec3(50.0f, 0.0f, 0.0f);

    nodes.push_back({"parent", parent});
    nodes.push_back({"child", child});

    auto results = TransformResolver3D::resolve(nodes);

    REQUIRE(results.find("parent") != results.end());
    REQUIRE(results.find("child") != results.end());

    CHECK_FALSE(results["parent"].cycle_detected);
    CHECK_FALSE(results["child"].cycle_detected);

    // Child world matrix should include parent position
    Vec4 child_origin = results["child"].world_matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(child_origin.x == doctest::Approx(150.0f));
}

TEST_CASE("TransformResolver3D: nested parenting") {
    std::vector<TransformResolver3D::InputNode> nodes;
    
    Transform3D grandparent;
    grandparent.position = Vec3(100.0f, 0.0f, 0.0f);

    Transform3D parent;
    parent.parent_name = "grandparent";
    parent.position = Vec3(50.0f, 0.0f, 0.0f);
    
    Transform3D child;
    child.parent_name = "parent";
    child.position = Vec3(25.0f, 0.0f, 0.0f);

    nodes.push_back({"grandparent", grandparent});
    nodes.push_back({"parent", parent});
    nodes.push_back({"child", child});

    auto results = TransformResolver3D::resolve(nodes);

    Vec4 child_origin = results["child"].world_matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(child_origin.x == doctest::Approx(175.0f));
}

TEST_CASE("TransformResolver3D: selective inheritance") {
    std::vector<TransformResolver3D::InputNode> nodes;
    
    Transform3D parent;
    parent.position = Vec3(100.0f, 0.0f, 0.0f);
    parent.rotation = Vec3(0.0f, 0.0f, 90.0f);
    parent.scale = Vec3(2.0f, 2.0f, 2.0f);
    
    Transform3D child_no_pos;
    child_no_pos.parent_name = "parent";
    child_no_pos.position = Vec3(50.0f, 0.0f, 0.0f);
    child_no_pos.inherits_position = false;

    Transform3D child_no_rot;
    child_no_rot.parent_name = "parent";
    child_no_rot.position = Vec3(50.0f, 0.0f, 0.0f);
    child_no_rot.inherits_rotation = false;

    nodes.push_back({"parent", parent});
    nodes.push_back({"child_no_pos", child_no_pos});
    nodes.push_back({"child_no_rot", child_no_rot});

    auto results = TransformResolver3D::resolve(nodes);

    // For child_no_pos, parent's position is ignored, but parent's rotation & scale are inherited.
    // Child's position is (50, 0, 0) rotated by 90 degrees around parent's scale (2, 2, 2)
    // (50*2, 0) rotated by 90 degrees -> (0, 100, 0)
    Vec4 p_no_pos = results["child_no_pos"].world_matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(p_no_pos.x == doctest::Approx(0.0f));
    CHECK(p_no_pos.y == doctest::Approx(100.0f));

    // For child_no_rot, parent's rotation is ignored, but position and scale are inherited.
    // Child position is (50, 0, 0) scaled by 2 -> (100, 0, 0) translated by parent's position (100, 0, 0) -> (200, 0, 0)
    Vec4 p_no_rot = results["child_no_rot"].world_matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(p_no_rot.x == doctest::Approx(200.0f));
    CHECK(p_no_rot.y == doctest::Approx(0.0f));
}

TEST_CASE("TransformResolver3D: cycle detection") {
    std::vector<TransformResolver3D::InputNode> nodes;
    
    Transform3D a;
    a.parent_name = "b";
    a.position = Vec3(10.0f, 0.0f, 0.0f);

    Transform3D b;
    b.parent_name = "a";
    b.position = Vec3(20.0f, 0.0f, 0.0f);

    nodes.push_back({"a", a});
    nodes.push_back({"b", b});

    auto results = TransformResolver3D::resolve(nodes);

    CHECK(results["a"].cycle_detected);
    CHECK(results["b"].cycle_detected);
}

TEST_CASE("TransformResolver3D: stability with identity transformations") {
    std::vector<TransformResolver3D::InputNode> nodes;
    
    Transform3D parent; // default identity
    Transform3D child;  // default identity
    child.parent_name = "parent";

    nodes.push_back({"parent", parent});
    nodes.push_back({"child", child});

    auto results = TransformResolver3D::resolve(nodes);

    CHECK(results["child"].world_matrix == Mat4(1.0f));
}
