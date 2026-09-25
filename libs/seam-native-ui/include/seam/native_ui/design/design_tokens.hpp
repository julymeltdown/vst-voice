#pragma once

#include "seam/native_ui/pixel_surface.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace seam::native_ui::design {

// The owner-approved visual modes. Mode is an application preference: it never enters a project
// file, a render request or a cache key, so switching it can never change audio.
enum class DesignMode : std::uint8_t { Emo, Scene };
enum class Contrast : std::uint8_t { Standard, High };

[[nodiscard]] std::string_view designModeName(DesignMode mode) noexcept;
[[nodiscard]] DesignMode parseDesignMode(std::string_view value,
                                         DesignMode fallback = DesignMode::Emo) noexcept;
[[nodiscard]] DesignMode otherDesignMode(DesignMode mode) noexcept;

// One role per meaning. A painter asks for a role; it never invents a color.
struct ColorRoles final {
  Color canvas, surface, surfaceRaised, surfaceSunken, border, borderStrong;
  Color textPrimary, textSecondary, textDisabled, textOnAccent;
  Color accent, accentDeep, accentCurve, accentTime, accentAlt1, accentAlt2;
  Color gridWeak, gridStrong, gridBar, keyWhite, keyBlack, keyLabel;
  Color noteFill, noteFillAlt, noteStroke, noteSelectedA, noteSelectedB, noteSelectedStroke;
  Color noteText, phonemeText, waveInNote, pitchCurve, pitchGlow;
  Color laneFillTop, laneFillBottom, knobTrack, knobBodyInner, knobBodyOuter, knobPointer;
  Color meterLow, meterMid, meterHigh, focusRing, selectionFill;
  Color warning, error, success, info, texturePrimary, textureSecondary;
};

// Sizes are logical points. Nothing essential is smaller than 11 points; 10 points is reserved for
// non-essential ruler ticks (docs/design/ui-fidelity-contract-v1.json).
struct TypeScale final {
  double body{13.0};
  double label{12.0};
  double lyric{13.0};
  double smallLabel{11.0};
  double rulerMicro{10.0};
  double panelTitle{12.0};
  double knobValue{16.0};
  double transport{22.0};
  double panelTitleTracking{2.2};
  double labelTracking{1.1};
};

struct ShapeTokens final {
  double note{6.0};
  double control{6.0};
  double card{12.0};
  double hero{14.0};
  double hairline{1.0};
};

struct LightTokens final {
  double glowSmall{6.0};
  double glowMedium{12.0};
  double glowLarge{22.0};
  double glowAlphaRest{0.20};
  double glowAlphaActive{0.55};
  double highlightAlpha{0.07};
  double textureAlpha{0.10};
};

struct DesignTokens final {
  DesignMode mode{DesignMode::Emo};
  Contrast contrast{Contrast::Standard};
  ColorRoles color;
  TypeScale type;
  ShapeTokens shape;
  LightTokens light;
  // Track colors cycle per track index; presentation only.
  std::array<Color, 5U> trackColors;
};

[[nodiscard]] const DesignTokens& tokensFor(DesignMode mode,
                                            Contrast contrast = Contrast::Standard) noexcept;

[[nodiscard]] constexpr Color withAlpha(Color color, double alpha) noexcept {
  const auto clamped = alpha < 0.0 ? 0.0 : (alpha > 1.0 ? 1.0 : alpha);
  return Color{color.red, color.green, color.blue,
               static_cast<std::uint8_t>(clamped * 255.0 + 0.5)};
}

[[nodiscard]] Color mix(Color a, Color b, double t) noexcept;

// WCAG relative-luminance contrast ratio of two opaque colors.
[[nodiscard]] double contrastRatio(Color foreground, Color background) noexcept;

}  // namespace seam::native_ui::design
