#pragma once
#include "seam/time/tick.hpp"
#include <cstdint>

namespace seam::voice_design::internal {
inline double excitationNoiseAt(std::uint64_t seed, time::SampleFrame frame) {
  auto x = seed ^ static_cast<std::uint64_t>(frame);
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27U)) * 0x94d049bb133111ebULL;
  x ^= x >> 31U;
  return 2.0 * static_cast<double>(x >> 11U) / 9007199254740992.0 - 1.0;
}
}
