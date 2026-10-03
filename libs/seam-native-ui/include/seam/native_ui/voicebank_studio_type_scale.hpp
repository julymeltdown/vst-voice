#pragma once

#include "seam/native_ui/design/design_tokens.hpp"

namespace seam::native_ui {

// The Voicebank Studio did not read the application type scale: each of its painters carried its own
// literal point sizes, and the readability repairs that brought them up to 12 point did not leave a
// floor behind them, so the next painter added to this surface could repeat the same drift. These are
// the sizes the Studio draws at, expressed as roles over design::TypeScale rather than as new
// numbers, so the scale owns the floor and a Studio painter chooses a role instead of choosing a
// size.
//
// The mapping is deliberately coarse. `label` is what a creator reads, so it is the default and the
// floor; `smallLabel` is the one role the Studio uses for text that is genuinely secondary and is
// never used for a value a creator has to read to act; `body` and `panelTitle` are the larger text of
// the designer rows and the section headings. Nothing here is below TypeScale::smallLabel, and the
// Studio floor is asserted rather than left to whoever reads this comment.
struct VoicebankStudioTypeScale final {
  // A value the creator reads to act: unit names, ids, states, counts, queue rows, job rows, the
  // detail block. This is the Studio's default and the floor.
  double label{design::TypeScale{}.label};
  // Text that is genuinely secondary and never carries a value a creator acts on: an empty-state
  // hint, the one-line note under a queue row. Still a readable size, never smaller.
  double secondary{design::TypeScale{}.smallLabel};
  // The designer-style rows (voice designer parameter rows, button labels) and section headings.
  double body{design::TypeScale{}.body};
  // The Studio's own window title and the largest heading a panel carries.
  double heading{design::TypeScale{}.body};
};

// The scale the Studio painters use. Returned by value so a caller cannot hold a pointer to a shared
// mutable global, and so the values are visible at every call site rather than behind a getter.
[[nodiscard]] constexpr VoicebankStudioTypeScale voicebankStudioTypeScale() noexcept {
  return {};
}

}
