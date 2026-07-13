#pragma once

// chronon3d/raster/bbox.hpp — forwarding shim for raster::BBox.
//
// Several translation units include this path, but the canonical
// definition of raster::BBox lives in <chronon3d/math/raster_utils.hpp>.
// This header exists to satisfy those includes without duplicating the
// struct definition.

#include <chronon3d/math/raster_utils.hpp>
