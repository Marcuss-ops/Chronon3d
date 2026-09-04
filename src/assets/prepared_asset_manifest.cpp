#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <sstream>
#include <string>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <spdlog/spdlog.h>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace chronon3d::assets {
namespace {

#include "prepared_asset_manifest_sha_detail.hpp"
#include "prepared_asset_manifest_support_detail.hpp"
#include "prepared_asset_manifest_digest_cache_detail.hpp"

} // namespace

#include "prepared_asset_manifest_prepare_detail.hpp"
#include "prepared_asset_manifest_verify_detail.hpp"
#include "prepared_asset_manifest_store_detail.hpp"

} // namespace chronon3d::assets
