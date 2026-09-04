#include <chronon3d/assets/mesh_loader.hpp>

#ifdef CHRONON3D_ENABLE_MESH
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace chronon3d::assets {
namespace {

#include "mesh_loader_support_detail.hpp"
#include "mesh_loader_materials_detail.hpp"
#include "mesh_loader_decode_detail.hpp"

} // namespace
#endif // CHRONON3D_ENABLE_MESH

#ifndef CHRONON3D_ENABLE_MESH
namespace chronon3d::assets {
#endif

#include "mesh_loader_api_detail.hpp"

} // namespace chronon3d::assets
