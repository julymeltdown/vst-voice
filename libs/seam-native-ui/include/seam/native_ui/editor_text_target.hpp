// The lyric id a free-text field commits to, shared by the editor controller's translation units.
//
// Text typed into a panel field belongs to no lyric the project holds, so it commits against one
// sentinel id instead of inventing a lyric. This was a function in editor_controller.cpp's
// anonymous namespace until the overlay and accessibility clusters moved into their own files,
// which left three translation units needing the same constant. A shared header states it once
// rather than copying the definition into each, because three copies of a sentinel id is three
// chances for them to drift apart and quietly stop matching.
//
// Internal to the editor controller's implementation; not part of the library's surface.
#pragma once

#include "seam/domain/ids.hpp"

#include <cstdint>
#include <limits>

namespace seam::native_ui {

// The largest representable token id, which no stored lyric can hold.
[[nodiscard]] inline domain::LyricTokenId externalTextTarget() noexcept {
  return domain::LyricTokenId{std::numeric_limits<std::uint64_t>::max()};
}

}  // namespace seam::native_ui
