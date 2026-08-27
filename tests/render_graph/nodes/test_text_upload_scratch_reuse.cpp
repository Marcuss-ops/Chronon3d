#include <doctest/doctest.h>

#include <chronon3d/render_graph/render_graph_context.hpp>

#include <cstddef>

TEST_SUITE("TextUploadScratch") {

TEST_CASE("ROI scratch capacity is reused across frames") {
    chronon3d::graph::RenderGraphContext context;
    auto& scratch = context.node_exec.text_upload_scratch;

    constexpr std::size_t roi_floats = 32u * 16u * 4u;
    scratch.resize(roi_floats);
    const auto capacity_after_first_frame = scratch.capacity();
    REQUIRE(capacity_after_first_frame >= roi_floats);

    scratch.clear();
    scratch.resize(roi_floats);
    const auto second_frame_data = scratch.data();
    CHECK(scratch.capacity() == capacity_after_first_frame);
    CHECK(second_frame_data != nullptr);

    // The second frame fits in the retained allocation.  Writing the ROI
    // must therefore not require a new backing allocation.
    const auto first_frame_data = scratch.data();
    scratch[0] = 1.0f;
    scratch.clear();
    scratch.resize(roi_floats);
    CHECK(scratch.capacity() == capacity_after_first_frame);
    CHECK(scratch.data() == first_frame_data);
}

TEST_CASE("larger ROI grows scratch, then smaller ROI reuses it") {
    chronon3d::graph::RenderGraphContext context;
    auto& scratch = context.node_exec.text_upload_scratch;

    scratch.resize(8u * 8u * 4u);
    const auto small_capacity = scratch.capacity();
    scratch.clear();
    scratch.resize(128u * 64u * 4u);
    const auto large_capacity = scratch.capacity();
    REQUIRE(large_capacity >= small_capacity);

    scratch.clear();
    scratch.resize(8u * 8u * 4u);
    CHECK(scratch.capacity() == large_capacity);
    CHECK(scratch.data() != nullptr);
}

TEST_CASE("cloned execution contexts do not share scratch ownership") {
    chronon3d::graph::RenderGraphContext context;
    context.node_exec.text_upload_scratch.resize(16u * 16u * 4u);
    const auto parent_capacity = context.node_exec.text_upload_scratch.capacity();

    auto clone = context.clone_for_node_execution();
    clone.node_exec.text_upload_scratch.resize(4u * 4u * 4u);

    CHECK(context.node_exec.text_upload_scratch.capacity() == parent_capacity);
    CHECK(clone.node_exec.text_upload_scratch.capacity() >= 4u * 4u * 4u);
}

} // TEST_SUITE
