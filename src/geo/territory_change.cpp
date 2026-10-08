#include "chronon3d/geo/territory_change.hpp"

#include "chronon3d/geo/geo_boolean.hpp"

namespace chronon3d::geo {

TerritoryChange compute_territory_change(const std::vector<GeoPolygon>& before,
                                         const std::vector<GeoPolygon>& after) {
    TerritoryChange out;
    out.shared = geo_boolean(GeoBooleanOp::Intersection, before, after);
    out.gained = geo_boolean(GeoBooleanOp::Difference, after, before);
    out.lost = geo_boolean(GeoBooleanOp::Difference, before, after);
    return out;
}

}  // namespace chronon3d::geo
