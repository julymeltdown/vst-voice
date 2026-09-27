#include "seam/native_ui/design/tooltip.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace seam::native_ui::design {
namespace {

using paint::Path;
using paint::StrokeStyle;
using paint::TextStyle;

bool sameId(const std::optional<TooltipSubject>& a, const std::optional<TooltipSubject>& b) {
  if (!a.has_value() || !b.has_value()) return a.has_value() == b.has_value();
  return a->id == b->id;
}

bool overlaps(ui::Rect a, ui::Rect b) noexcept {
  return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

// The byte length of the longest prefix of text that is whole UTF-8 characters and at most limit.
std::size_t utf8Prefix(std::string_view text, std::size_t limit) noexcept {
  limit = std::min(limit, text.size());
  while (limit > 0U && limit < text.size() &&
         (static_cast<unsigned char>(text[limit]) & 0xC0U) == 0x80U)
    --limit;
  return limit;
}

std::size_t utf8Next(std::string_view text, std::size_t at) noexcept {
  if (at >= text.size()) return text.size();
  ++at;
  while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0U) == 0x80U) ++at;
  return at;
}

std::string_view trim(std::string_view text) noexcept {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1U);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1U);
  return text;
}

}  // namespace

bool tooltipRole(SemanticRole role) noexcept {
  switch (role) {
    case SemanticRole::Button:
    case SemanticRole::Tab:
    case SemanticRole::RadioButton:
    case SemanticRole::CheckBox:
    case SemanticRole::Slider:
    case SemanticRole::Status:
    case SemanticRole::ProgressIndicator:
      return true;
    default:
      return false;
  }
}

void TooltipTimer::hover(std::optional<TooltipSubject> subject, Clock::time_point now) {
  if (sameId(hovered_, subject)) {
    hovered_ = std::move(subject);
    if (!keyboard_ && hovered_ && current_) *current_ = *hovered_;
    return;
  }
  hovered_ = std::move(subject);
  // The pointer moved onto something else: it is the pointer's tip now, unless it only left a
  // target while the keyboard's candidate stands.
  if (hovered_.has_value() || !focused_.has_value()) keyboard_ = false;
  retarget(now);
}

void TooltipTimer::focus(std::optional<TooltipSubject> subject, Clock::time_point now) {
  if (sameId(focused_, subject)) {
    focused_ = std::move(subject);
    if (keyboard_ && focused_ && current_) *current_ = *focused_;
    return;
  }
  focused_ = std::move(subject);
  keyboard_ = focused_.has_value();
  retarget(now);
}

void TooltipTimer::retarget(Clock::time_point now) {
  const auto& next = keyboard_ ? focused_ : hovered_;
  if (sameId(current_, next)) {
    current_ = next;
    return;
  }
  if (current_.has_value() && !dismissed_ && now >= showAt_) handoffFrom_ = now;
  current_ = next;
  dismissed_ = false;
  const auto handoff = handoffFrom_.has_value() && now - *handoffFrom_ <= kTooltipHandoff;
  showAt_ = handoff ? now : now + kTooltipDelay;
}

void TooltipTimer::dismiss() noexcept {
  dismissed_ = true;
  handoffFrom_.reset();
}

void TooltipTimer::reset() noexcept { *this = TooltipTimer{}; }

const TooltipSubject* TooltipTimer::shown(Clock::time_point now) const noexcept {
  if (!current_.has_value() || dismissed_ || now < showAt_ || current_->text.empty()) return nullptr;
  return &*current_;
}

std::optional<TooltipTimer::Clock::time_point> TooltipTimer::showsAt() const noexcept {
  if (!current_.has_value() || dismissed_ || current_->text.empty()) return std::nullopt;
  return showAt_;
}

TextStyle tooltipTextStyle(const DesignTokens& tokens) noexcept {
  return TextStyle{paint::FontRole::Ui, tokens.type.label, 0.0, paint::TextAlign::Left, false};
}

std::vector<std::string> wrapTooltipText(std::string_view text, double maxWidth,
                                         const TextStyle& style, const TooltipMeasure& measure,
                                         std::size_t maxLines) {
  std::vector<std::string> lines;
  if (maxLines == 0U || maxWidth <= 0.0) return lines;
  const auto fits = [&](std::string_view line) { return measure(line, style) <= maxWidth; };
  const auto pushWord = [&](std::string& line, std::string_view word) {
    // A word longer than a whole line is broken at the last character that still fits.
    while (!word.empty() && !fits(word)) {
      std::size_t cut = utf8Next(word, 0U);
      for (std::size_t next = utf8Next(word, cut); next < word.size() && fits(word.substr(0U, next));
           next = utf8Next(word, next))
        cut = next;
      lines.emplace_back(word.substr(0U, utf8Prefix(word, cut)));
      word.remove_prefix(utf8Prefix(word, cut));
    }
    line.assign(word);
  };
  std::size_t start = 0U;
  while (start <= text.size()) {
    const auto end = std::min(text.find('\n', start), text.size());
    const auto paragraph = trim(text.substr(start, end - start));
    std::string line;
    std::size_t at = 0U;
    while (at < paragraph.size()) {
      auto space = paragraph.find(' ', at);
      if (space == std::string_view::npos) space = paragraph.size();
      const auto word = paragraph.substr(at, space - at);
      at = space + 1U;
      if (word.empty()) continue;
      if (line.empty()) {
        pushWord(line, word);
        continue;
      }
      std::string candidate = line;
      candidate.push_back(' ');
      candidate.append(word);
      if (fits(candidate)) {
        line = std::move(candidate);
      } else {
        lines.push_back(std::move(line));
        line.clear();
        pushWord(line, word);
      }
    }
    if (!line.empty()) lines.push_back(std::move(line));
    if (end >= text.size()) break;
    start = end + 1U;
  }
  if (lines.size() > maxLines) {
    std::string last = lines[maxLines - 1U];
    for (std::size_t i = maxLines; i < lines.size(); ++i) last += " " + lines[i];
    lines.resize(maxLines);
    lines.back() = std::move(last);
  }
  return lines;
}

TooltipLayout layoutTooltip(std::string_view text, ui::Rect target, ui::Rect window,
                            const DesignTokens& tokens, const TooltipMeasure& measure) {
  TooltipLayout layout;
  layout.style = tooltipTextStyle(tokens);
  layout.lineHeight = std::ceil(layout.style.size * 1.34);
  const ui::Rect region{window.x + kTooltipMargin, window.y + kTooltipMargin,
                        window.width - 2.0 * kTooltipMargin, window.height - 2.0 * kTooltipMargin};
  if (trim(text).empty() || region.width < 2.0 * kTooltipPaddingX + 24.0 ||
      region.height < 2.0 * kTooltipPaddingY + layout.lineHeight)
    return {};
  const auto boxFor = [&](double innerWidth, std::size_t maxLines) {
    auto lines = wrapTooltipText(text, innerWidth, layout.style, measure, maxLines);
    double widest = 0.0;
    for (const auto& line : lines) widest = std::max(widest, measure(line, layout.style));
    const auto width = std::ceil(std::min(innerWidth, widest) + 2.0 * kTooltipPaddingX);
    const auto height = static_cast<double>(lines.size()) * layout.lineHeight + 2.0 * kTooltipPaddingY;
    return std::pair{std::move(lines), ui::Size{width, height}};
  };
  const auto clampX = [&](double x, double w) {
    return std::clamp(x, region.x, std::max(region.x, region.right() - w));
  };
  const auto clampY = [&](double y, double h) {
    return std::clamp(y, region.y, std::max(region.y, region.bottom() - h));
  };
  const auto linesFor = [&](double height) {
    const auto n = std::floor((height - 2.0 * kTooltipPaddingY) / layout.lineHeight);
    return n < 1.0 ? std::size_t{0U} : std::min(kTooltipMaxLines, static_cast<std::size_t>(n));
  };
  const auto accept = [&](std::vector<std::string> lines, ui::Rect box) {
    if (lines.empty() || overlaps(box, target)) return false;
    if (box.x < region.x - 0.01 || box.y < region.y - 0.01 || box.right() > region.right() + 0.01 ||
        box.bottom() > region.bottom() + 0.01)
      return false;
    layout.lines = std::move(lines);
    layout.box = box;
    return true;
  };
  const auto inner = std::min(kTooltipMaxWidth, region.width) - 2.0 * kTooltipPaddingX;
  const auto centerX = target.x + target.width * 0.5;
  const auto centerY = target.y + target.height * 0.5;
  const auto below = target.bottom() + kTooltipGap;
  const auto above = target.y - kTooltipGap;
  // Vertical placements first: below, then above, each with as many lines as that side holds.
  for (const auto side : {0, 1}) {
    const auto space = side == 0 ? region.bottom() - below : above - region.y;
    const auto maxLines = linesFor(space);
    if (maxLines == 0U) continue;
    auto [lines, size] = boxFor(inner, maxLines);
    const auto y = side == 0 ? below : above - size.height;
    if (accept(std::move(lines), {clampX(centerX - size.width * 0.5, size.width), y, size.width,
                                  size.height}))
      return layout;
  }
  // Then beside the target, narrowed to the room on that side.
  for (const auto side : {0, 1}) {
    const auto space = side == 0 ? region.right() - (target.right() + kTooltipGap)
                                 : (target.x - kTooltipGap) - region.x;
    const auto sideInner = std::min(inner, space - 2.0 * kTooltipPaddingX);
    if (sideInner < 24.0) continue;
    auto [lines, size] = boxFor(sideInner, linesFor(region.height));
    const auto x = side == 0 ? target.right() + kTooltipGap : target.x - kTooltipGap - size.width;
    if (accept(std::move(lines), {x, clampY(centerY - size.height * 0.5, size.height), size.width,
                                  size.height}))
      return layout;
  }
  return {};
}

void paintTooltip(paint::Canvas2D& c, const DesignTokens& t, const TooltipLayout& layout) {
  if (layout.empty()) return;
  const auto high = t.contrast == Contrast::High;
  const auto shape = Path::roundedRect(layout.box, kTooltipRadius);
  c.save();
  // Standard: the plan's 12-point glow at 0.18. High Contrast draws none (the recorder drops glow
  // there too) and relies on an opaque card and a text-colored border.
  if (!high) c.setGlow(withAlpha(t.color.accent, 0.18), t.light.glowMedium);
  c.fill(shape, t.color.surfaceRaised);
  c.restore();
  c.stroke(shape, high ? t.color.textPrimary : withAlpha(t.color.borderStrong, 0.95),
           StrokeStyle{high ? 2.0 : 1.0});
  for (std::size_t i = 0U; i < layout.lines.size(); ++i) {
    const ui::Rect line{layout.box.x + kTooltipPaddingX,
                        layout.box.y + kTooltipPaddingY + static_cast<double>(i) * layout.lineHeight,
                        layout.box.width - 2.0 * kTooltipPaddingX, layout.lineHeight};
    c.text(line, layout.lines[i], layout.style, t.color.textPrimary);
  }
}

}  // namespace seam::native_ui::design

