#pragma once

#include "seam/domain/performance_intent.hpp"
#include "seam/formats/json_value.hpp"

namespace seam::formats::detail {

[[nodiscard]] JsonValue encodeRegionPerformance(const domain::RegionPerformanceState& state);
[[nodiscard]] core::Result<domain::RegionPerformanceState> decodeRegionPerformance(const JsonValue* value);

}
