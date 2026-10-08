#pragma once

#include <string_view>
#include <vector>

#include "chronon3d/geo/geo_feature.hpp"

namespace chronon3d::geo {

/// Returns the features whose property `key` equals `value`, in collection order.
/// Pointers refer into `collection` and are valid while it is alive.
[[nodiscard]] std::vector<const GeoFeature*> query_by_property(
    const GeoFeatureCollection& collection, std::string_view key, std::string_view value);

}  // namespace chronon3d::geo
