#pragma once

#include "seam/native_ui/paint/canvas2d.hpp"

#include <string>
#include <string_view>

namespace seam::native_ui::design {

// Shrink a label until it fits, never below `floor`. The floor is a parameter
// rather than a constant because the two workspaces had genuinely different
// ones -- SING allowed 10, MIX 11 -- and folding them together changed how much
// text a label could still show, which broke MIX's elision contract when the two
// were first merged. A shared helper must not silently move either.
[[nodiscard]] inline paint::TextStyle fitted(paint::Canvas2D& c, std::string_view text,
                                             paint::TextStyle s, double width,
                                             double floor = 11.0) {
  if (width <= 0.0 || c.measure(text, s) <= width) return s;
  s.tracking = std::min(s.tracking, 0.4);
  while (s.size > floor && c.measure(text, s) > width) s.size = std::max(floor, s.size - 0.5);
  return s;
}

// Trim the middle of a label rather than its tail, so a name keeps both the
// family it shares with its neighbours and the part that distinguishes it.
// Truncating the tail is what turned "Diagnostic dry vocal render" into
// "DIAGNOSTIC DRY...", which reads as a category rather than a track, and what
// hid the word "Fixture" from a bank name whose honest identity is its ending.
//
// Shared rather than repeated: SING, MIX and the rack all have fixed-width
// boxes, and three copies of this rule would drift apart the first time one of
// them was tuned.
[[nodiscard]] inline std::string midEllipsis(paint::Canvas2D& c, std::string_view text,
                                             const paint::TextStyle& s, double width) {
  if (width <= 0.0 || text.empty() || c.measure(text, s) <= width) return std::string{text};
  static constexpr std::string_view kMarker = "\xE2\x80\xA6";  // U+2026 HORIZONTAL ELLIPSIS
  if (c.measure(kMarker, s) > width) return {};
  std::size_t head = 0;
  std::size_t tail = 0;
  while (head + tail < text.size()) {
    const std::string candidate =
        std::string{text.substr(0, head + 1U)} + std::string{kMarker} +
        std::string{text.substr(text.size() - (tail + 1U))};
    if (c.measure(candidate, s) > width) break;
    ++head;
    ++tail;
  }
  if (head == 0U || tail == 0U) {
    // Not one character survives at each end: keep a head-only trim, which
    // beats returning nothing and beats overflowing the box.
    std::size_t kept = 0;
    while (kept < text.size() &&
           c.measure(std::string{text.substr(0, kept + 1U)} + std::string{kMarker}, s) <= width) {
      ++kept;
    }
    return kept == 0U ? std::string{} : std::string{text.substr(0, kept)} + std::string{kMarker};
  }
  return std::string{text.substr(0, head)} + std::string{kMarker} +
         std::string{text.substr(text.size() - tail)};
}

}  // namespace seam::native_ui::design
