#pragma once

// The kit's tooltip (redesign plan section 4, "Tooltip / Popover"). One small card that explains a
// control the pointer rests on or the keyboard focuses: an icon-only button, a refused knob, a label
// the frame had to elide. It is presentation only. Its text is what the control already publishes
// to accessibility (the node's description, or the whole text of an elided label, which the node
// carries at that place), so a screen reader hears the same words and no separate node is added.
//
// Rules (docs/design/NATIVE_EDITOR_DESIGN_SYSTEM.md section 4):
// - it appears after the pointer rests on one target for kTooltipDelay, or the keyboard focuses a
//   target and holds it that long; moving straight from one shown tip to the next target shows the
//   next one at once (kTooltipHandoff);
// - Escape, a press, a scroll or typing hides it until the pointer or focus moves to another target;
// - it is placed inside the window, below its target when there is room, else above, right or left,
//   and it never covers the target (so never the element under the pointer);
// - it wraps to at most kTooltipMaxLines lines of kTooltipMaxWidth points;
// - High Contrast draws it opaque with a 2-point text-colored border and no glow.

#include "seam/native_ui/design/design_tokens.hpp"
#include "seam/native_ui/editor_semantics.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/ui/geometry.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seam::native_ui::design {

// The plan says 400 ms; the shell uses 600 ms, closer to the platform's own help tags, so a pointer
// crossing the header on its way somewhere else does not flash tips.
inline constexpr std::chrono::milliseconds kTooltipDelay{600};
inline constexpr std::chrono::milliseconds kTooltipHandoff{300};
inline constexpr double kTooltipMargin = 8.0;
inline constexpr double kTooltipGap = 6.0;
inline constexpr double kTooltipMaxWidth = 320.0;
inline constexpr double kTooltipRadius = 8.0;
inline constexpr double kTooltipPaddingX = 10.0;
inline constexpr double kTooltipPaddingY = 6.0;
inline constexpr std::size_t kTooltipMaxLines = 6U;

// Roles that can carry a tooltip: controls and read-only status items. Notes, the timeline, lanes,
// panels and text fields never show one; the pointer over them belongs to editing.
[[nodiscard]] bool tooltipRole(SemanticRole role) noexcept;

// What a tooltip explains: the published node (or elided label) it belongs to, the rectangle it
// must never cover, and its text.
struct TooltipSubject final {
  std::string id;
  ui::Rect target;
  std::string text;
};

// When a tooltip is shown. The shell reports the subject under the pointer and the subject that
// has keyboard focus; the most recent of the two to change is the candidate, and it shows once its
// delay has passed. The clock is the caller's, so a frozen test clock freezes the delay.
class TooltipTimer final {
public:
  using Clock = std::chrono::steady_clock;

  // The subject the pointer rests on, or nothing. The same subject again (same id) only refreshes
  // its rectangle and text; it does not restart the delay.
  void hover(std::optional<TooltipSubject> subject, Clock::time_point now);
  // The subject with keyboard focus, or nothing, with the same refresh rule.
  void focus(std::optional<TooltipSubject> subject, Clock::time_point now);
  // Escape, a press, a scroll or typing. The candidate stays hidden until it changes.
  void dismiss() noexcept;
  // Forgets everything (the shell was hidden, the workspace changed).
  void reset() noexcept;

  [[nodiscard]] const TooltipSubject* shown(Clock::time_point now) const noexcept;
  // When the candidate will show, while it is waiting; nothing when there is none or it was hidden.
  [[nodiscard]] std::optional<Clock::time_point> showsAt() const noexcept;
  [[nodiscard]] const TooltipSubject* candidate() const noexcept {
    return current_ ? &*current_ : nullptr;
  }
  // True when the candidate came from keyboard focus.
  [[nodiscard]] bool fromKeyboard() const noexcept { return keyboard_; }

private:
  void retarget(Clock::time_point now);

  std::optional<TooltipSubject> hovered_;
  std::optional<TooltipSubject> focused_;
  bool keyboard_{false};
  std::optional<TooltipSubject> current_;
  Clock::time_point showAt_{};
  bool dismissed_{false};
  // When a shown tip last went away because the candidate changed; a new target within the handoff
  // window shows at once.
  std::optional<Clock::time_point> handoffFrom_;
};

// A tooltip placed in a window: its card and its wrapped lines.
struct TooltipLayout final {
  ui::Rect box{};
  std::vector<std::string> lines;
  paint::TextStyle style{};
  double lineHeight{16.0};
  [[nodiscard]] bool empty() const noexcept { return lines.empty() || box.width <= 0.0; }
};

using TooltipMeasure = std::function<double(std::string_view, const paint::TextStyle&)>;

[[nodiscard]] paint::TextStyle tooltipTextStyle(const DesignTokens& tokens) noexcept;
// Wraps text to lines no wider than maxWidth (words, then characters for a word longer than a
// line); lines past maxLines are folded into the last one, which the canvas then elides.
[[nodiscard]] std::vector<std::string> wrapTooltipText(std::string_view text, double maxWidth,
                                                       const paint::TextStyle& style,
                                                       const TooltipMeasure& measure,
                                                       std::size_t maxLines = kTooltipMaxLines);
// Places the tooltip for target inside window. The box lies inside the window less
// kTooltipMargin and never intersects target; it is empty when no side has room for one line.
[[nodiscard]] TooltipLayout layoutTooltip(std::string_view text, ui::Rect target, ui::Rect window,
                                          const DesignTokens& tokens,
                                          const TooltipMeasure& measure);
void paintTooltip(paint::Canvas2D& canvas, const DesignTokens& tokens, const TooltipLayout& layout);

}  // namespace seam::native_ui::design
