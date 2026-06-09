#pragma once

#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <memory_resource>
#include <string>

namespace chronon3d {

class RenderNodeFactory {
public:
    static RenderNode rect(std::pmr::memory_resource* res, std::string name, const RectParams& p);
    static RenderNode rounded_rect(std::pmr::memory_resource* res, std::string name, const RoundedRectParams& p);
    static RenderNode circle(std::pmr::memory_resource* res, std::string name, const CircleParams& p);
    static RenderNode line(std::pmr::memory_resource* res, std::string name, const LineParams& p);
    static RenderNode path(std::pmr::memory_resource* res, std::string name, PathParams p);
    static RenderNode image(std::pmr::memory_resource* res, std::string name, ImageParams p);
    static RenderNode tiled_image(std::pmr::memory_resource* res, std::string name, ImageParams p);
    static RenderNode grid_background(std::pmr::memory_resource* res, std::string name, const GridBackgroundParams& p);
    static RenderNode text(std::pmr::memory_resource* res, std::string name, TextParams p);
    
    // Specialized 3D Shapes
    static RenderNode fake_box3d(std::pmr::memory_resource* res, std::string name, const FakeBox3DParams& p);
    static RenderNode grid_plane(std::pmr::memory_resource* res, std::string name, const GridPlaneParams& p);

private:
    static RenderNode base(std::pmr::memory_resource* res, std::string name);
};

} // namespace chronon3d
