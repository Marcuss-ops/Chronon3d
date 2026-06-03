#pragma once

#include <chronon3d/scene/transform/transform_3d.hpp>
#include <string>

namespace chronon3d {

struct NullParams {
    std::string name;
    Transform3D transform{};
    bool visible_in_diagnostics{true};
};

class NullBuilder {
public:
    explicit NullBuilder(NullParams& p) : params(p) {}

    NullBuilder& position(Vec3 v) {
        params.transform.position = v;
        return *this;
    }

    NullBuilder& rotation(Vec3 deg) {
        params.transform.rotation = deg;
        return *this;
    }

    NullBuilder& scale(Vec3 s) {
        params.transform.scale = s;
        return *this;
    }

    NullBuilder& anchor(Vec3 a) {
        params.transform.anchor = a;
        return *this;
    }

    NullBuilder& parent(std::string name) {
        params.transform.parent_name = std::move(name);
        return *this;
    }

    NullBuilder& visible_in_diagnostics(bool v) {
        params.visible_in_diagnostics = v;
        return *this;
    }

private:
    NullParams& params;
};

} // namespace chronon3d
