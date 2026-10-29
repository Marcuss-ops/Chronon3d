#include <iostream>
#include <nlohmann/json.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

int main() {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":320,"height":180,"fps_num":30,"fps_den":1,"duration_frames":30},
      "layers":[{"id":"title","type":"text","text":"Index",
        "style":{"font":"fonts/roboto.ttf","font_size":24,"fill":"#FFFFFF"},
        "text_animators":[{"id":"n","selectors":[
          {"shape":"noise","noise_frequency":0.8,"noise_amplitude":0.7,"noise_seed":99},
          {"order":"index","index_step":3}],
          "properties":[{"property":"position_y","keyframes":[{"frame":0,"value":4}]}]}]}],
      "output":{"path":"out.png"}})JSON");
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    if (decoded.has_value()) {
        std::cout << "OK layers=" << decoded->layers.size() << "\n";
        for (const auto& l : decoded->layers) {
            std::cout << "  layer id=" << l.id
                      << " type=" << static_cast<int>(l.type)
                      << " text_animators=" << l.text_animators.size() << "\n";
        }
        if (!decoded->layers[0].text_animators.empty()) {
            const auto& sel = decoded->layers[0].text_animators[0].selectors;
            std::cout << "  selectors=" << sel.size() << "\n";
            for (const auto& s : sel) std::cout << "    shape=" << s.shape << "\n";
        } else {
            std::cout << "  ! text_animators EMPTY\n";
        }
    } else {
        std::cout << "ERR: " << decoded.error().path << ": " << decoded.error().message << "\n";
    }
    return 0;
}
