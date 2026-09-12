#pragma once
#include <cstdint>
namespace seam::voice_design {
struct FricationConfig final {
  std::uint64_t seed{0U};
  double centerHz{5000.0}, bandwidthHz{3000.0}, gain{0.15};
  friend bool operator==(const FricationConfig&, const FricationConfig&) = default;
};
}
