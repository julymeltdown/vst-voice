#include "cerulean_faceplate.hpp"

#include "seam/native_ui/design/design_tokens.hpp"
#include "seam/native_ui/design/shell_strings.hpp"
#include "seam/native_ui/design/sing_layout.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/region_envelope.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace seam::native_ui::design {
namespace {

using paint::Canvas2D;
using paint::FontRole;
using paint::Path;
using paint::StrokeStyle;
using paint::TextAlign;
using paint::TextStyle;

void paintHalftone(Canvas2D& c, ui::Rect band, Color ink) {
  Path dots;
  const double left = band.x + (band.width > 620 ? 320.0 : 136.0);
  for (double y = band.y + 4; y < band.bottom() - 3; y += 5) {
    for (double x = left; x < band.right() - 3; x += 5) {
      const double u = (x - left) / std::max(1.0, band.right() - left);
      const double cloud = std::sin(x * 0.023 + y * 0.09) + 0.6 * std::sin(x * 0.053 - y * 0.17);
      const double radius = std::clamp(1.2 + cloud + u * 1.5, 0.15, 3.0);
      dots.arc({x, y}, radius, 0.0, 2.0 * std::numbers::pi).close();
    }
  }
  c.fill(dots, ink);
}

}  // namespace

void paintCeruleanFaceplate(Canvas2D& c, const DesignTokens& t, const SingLayout& l) {
  const auto& band = l.masthead;
  c.fill(Path::rect({0, 0, l.width, l.height}), t.color.canvas);
  c.stroke(Path::rect({5, 5, l.width - 10, l.height - 10}), t.color.textPrimary, StrokeStyle{1});
  c.fill(Path::rect(band), t.color.surface);
  const auto rule = [&](ui::Rect box) {
    if (box.width > 0 && box.height > 0) {
      c.fill(Path::rect(box), t.color.surface);
      c.stroke(Path::rect(box), t.color.borderStrong, StrokeStyle{1});
    }
  };
  rule(l.header);
  rule(l.editor);
  rule(l.lane);
  rule(l.rackArea);
  rule(l.status);
  if (l.rack == RackPresentation::Full) {
    rule(l.expression);
    rule(l.style);
  }
  // An irregular dot screen is printed into the masthead only. Its cloud edges
  // stay in logical coordinates when the host changes the backing scale.
  if (t.contrast == Contrast::Standard) {
    paintHalftone(c, band, t.color.texturePrimary);
  }
  c.text(l.wordmark, tr(Str::SEAM),
         TextStyle{FontRole::UiBold, std::min(36.0, band.height - 3), 0.8}, t.color.textPrimary);
  if (band.width > 620)
    c.text({l.wordmark.right() + 4, band.y, 186, band.height}, tr(Str::SingingVoiceSynthesizer),
           TextStyle{FontRole::Mono, 12, 0, TextAlign::Left, true}, t.color.textSecondary);
  rule(l.transport);
}

void paintCeruleanWaveform(Canvas2D& c, const DesignTokens& t, const SingLayout& l,
                           const RegionWaveform& waveform) {
  const ui::Rect lcd{l.singer.x + 16, l.singer.y + 48, l.singer.width - 32,
                     l.singerChange.y - l.singer.y - 62};
  c.fill(Path::rect(lcd), t.contrast == Contrast::High ? t.color.surface : t.color.textureSecondary);
  c.stroke(Path::rect(lcd), t.color.borderStrong, StrokeStyle{1});
  const auto mid = lcd.y + lcd.height / 2;
  Path trace;
  trace.moveTo({lcd.x + 6, mid}).lineTo({lcd.right() - 6, mid});
  if (waveform.shown() && waveform.view->envelope) {
    const auto& envelope = *waveform.view->envelope;
    const int columns = static_cast<int>((lcd.width - 16) / 3);
    const auto frames = static_cast<std::int64_t>(envelope.sampleCount());
    const auto heightScale = (lcd.height - 12) * 0.5 / std::max(0.001F, envelope.peak());
    for (int i = 0; i < columns; ++i) {
      const auto pair = envelope.range(frames * i / columns, frames * (i + 1) / columns);
      if (!pair) continue;
      const auto x = lcd.x + 8 + 3 * i;
      trace.moveTo({x, mid - pair->maximum * heightScale})
           .lineTo({x, mid - pair->minimum * heightScale});
    }
  } else {
    c.text({lcd.x + 8, lcd.y + 3, lcd.width - 16, 18}, waveform.caption,
           TextStyle{FontRole::Mono, t.type.smallLabel}, t.color.textSecondary);
  }
  c.stroke(trace, t.color.textPrimary, StrokeStyle{1.4});
}

}  // namespace seam::native_ui::design
