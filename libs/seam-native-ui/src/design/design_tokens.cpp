#include "seam/native_ui/design/design_tokens.hpp"

#include <algorithm>
#include <cmath>

namespace seam::native_ui::design {
namespace {

constexpr Color hex(std::uint32_t rgb, std::uint8_t alpha = 255U) noexcept {
  return Color{static_cast<std::uint8_t>((rgb >> 16U) & 0xFFU),
               static_cast<std::uint8_t>((rgb >> 8U) & 0xFFU),
               static_cast<std::uint8_t>(rgb & 0xFFU), alpha};
}

DesignTokens makeEmo(Contrast contrast) noexcept {
  DesignTokens t;
  t.mode = DesignMode::Emo;
  t.contrast = contrast;
  auto& c = t.color;
  // Ink black, bone white, blood red, steel. Stage-light drama on dark glass.
  c.canvas = hex(0x0B0A0C);
  c.surface = hex(0x131115);
  c.surfaceRaised = hex(0x1B181D);
  c.surfaceSunken = hex(0x08070A);
  c.border = hex(0x2E2A31);
  c.borderStrong = hex(0x4A434F);
  c.textPrimary = hex(0xEDE8E3);
  c.textSecondary = hex(0xA39A9F);
  c.textDisabled = hex(0x625B61);
  c.textOnAccent = hex(0xFFF6F2);
  c.accent = hex(0xD1143A);
  c.accentDeep = hex(0x7A0C22);
  c.accentCurve = hex(0xF0E6E2);
  c.accentTime = hex(0xFF2D4F);
  c.accentAlt1 = hex(0xB8BCC6);
  c.accentAlt2 = hex(0xC8553D);
  c.gridWeak = hex(0x17151A);
  c.gridStrong = hex(0x242027);
  c.gridBar = hex(0x3A3238);
  c.keyWhite = hex(0xE6E1DC);
  c.keyBlack = hex(0x121014);
  c.keyLabel = hex(0x3B3438);
  c.noteFill = hex(0x2A1218);
  c.noteFillAlt = hex(0x351620);
  c.noteStroke = hex(0xB0122F);
  c.noteSelectedA = hex(0xD1143A);
  c.noteSelectedB = hex(0x8E0F2A);
  c.noteSelectedStroke = hex(0xFF5A73);
  c.noteText = hex(0xF3EEEA);
  c.phonemeText = hex(0xB9A9AE);
  c.waveInNote = hex(0xF2D6DB);
  c.pitchCurve = hex(0xFF2D4F);
  c.pitchGlow = hex(0xD1143A);
  c.laneFillTop = hex(0xD1143A);
  c.laneFillBottom = hex(0x3A0710);
  c.knobTrack = hex(0x2B262E);
  c.knobBodyInner = hex(0x221E25);
  c.knobBodyOuter = hex(0x121015);
  c.knobPointer = hex(0xEDE8E3);
  c.meterLow = hex(0x8E0F2A);
  c.meterMid = hex(0xD1143A);
  c.meterHigh = hex(0xFF5A73);
  c.focusRing = hex(0xF4D9A0);
  c.selectionFill = hex(0xD1143A, 46U);
  c.warning = hex(0xE0A040);
  c.error = hex(0xFF3355);
  c.success = hex(0xB8D0C0);
  c.info = hex(0xB8BCC6);
  c.texturePrimary = hex(0xD1143A);
  c.textureSecondary = hex(0xEDE8E3);
  t.trackColors = {hex(0xD1143A), hex(0xB8BCC6), hex(0xEDE8E3), hex(0xC8553D), hex(0x6F6A73)};
  if (contrast == Contrast::High) {
    c.border = hex(0x8A8A8A);
    c.textSecondary = hex(0xD6D0CC);
    c.gridStrong = hex(0x4A444C);
    t.light.textureAlpha = 0.0;
  }
  return t;
}

DesignTokens makeScene(Contrast contrast) noexcept {
  DesignTokens t;
  t.mode = DesignMode::Scene;
  t.contrast = contrast;
  auto& c = t.color;
  // Neon pink, cyan, lime and violet over deep night glass; glitter and checker for attitude.
  c.canvas = hex(0x0D0716);
  c.surface = hex(0x160D23);
  c.surfaceRaised = hex(0x1F1131);
  c.surfaceSunken = hex(0x0A0512);
  c.border = hex(0x3A2358);
  c.borderStrong = hex(0x5B3A86);
  c.textPrimary = hex(0xFFF4FB);
  c.textSecondary = hex(0xC9A8D8);
  c.textDisabled = hex(0x6C5680);
  c.textOnAccent = hex(0x1A0714);
  c.accent = hex(0xFF2E9A);
  c.accentDeep = hex(0xB0156A);
  c.accentCurve = hex(0x1DE9FF);
  c.accentTime = hex(0xB8FF3B);
  c.accentAlt1 = hex(0x9B5CFF);
  c.accentAlt2 = hex(0xFFE14D);
  c.gridWeak = hex(0x160D23);
  c.gridStrong = hex(0x251538);
  c.gridBar = hex(0x3E2560);
  c.keyWhite = hex(0xEDE3F4);
  c.keyBlack = hex(0x120A1C);
  c.keyLabel = hex(0x4A3666);
  c.noteFill = hex(0x2A1540);
  c.noteFillAlt = hex(0x0E2A3A);
  c.noteStroke = hex(0xFF2E9A);
  c.noteSelectedA = hex(0xFF2E9A);
  c.noteSelectedB = hex(0x9B5CFF);
  c.noteSelectedStroke = hex(0x1DE9FF);
  c.noteText = hex(0xFFF4FB);
  c.phonemeText = hex(0xC9A8D8);
  c.waveInNote = hex(0x7FF4FF);
  c.pitchCurve = hex(0x1DE9FF);
  c.pitchGlow = hex(0x1DE9FF);
  c.laneFillTop = hex(0x1DE9FF);
  c.laneFillBottom = hex(0x0A2A40);
  c.knobTrack = hex(0x2E1C46);
  c.knobBodyInner = hex(0x2A1740);
  c.knobBodyOuter = hex(0x130A20);
  c.knobPointer = hex(0xFFF4FB);
  c.meterLow = hex(0x1DE9FF);
  c.meterMid = hex(0xFF2E9A);
  c.meterHigh = hex(0xB8FF3B);
  c.focusRing = hex(0xB8FF3B);
  c.selectionFill = hex(0xFF2E9A, 46U);
  c.warning = hex(0xFFE14D);
  c.error = hex(0xFF4D6D);
  c.success = hex(0x7CFFB2);
  c.info = hex(0x1DE9FF);
  c.texturePrimary = hex(0xFF2E9A);
  c.textureSecondary = hex(0x1DE9FF);
  t.trackColors = {hex(0xFF2E9A), hex(0x1DE9FF), hex(0xB8FF3B), hex(0x9B5CFF), hex(0xFFE14D)};
  if (contrast == Contrast::High) {
    c.border = hex(0xB99AD6);
    c.textSecondary = hex(0xE6D2F0);
    c.gridStrong = hex(0x4A3270);
    t.light.textureAlpha = 0.0;
  }
  return t;
}

}  // namespace

std::string_view designModeName(DesignMode mode) noexcept {
  return mode == DesignMode::Scene ? "scene" : "emo";
}

DesignMode parseDesignMode(std::string_view value, DesignMode fallback) noexcept {
  if (value == "emo" || value == "EMO") return DesignMode::Emo;
  if (value == "scene" || value == "SCENE") return DesignMode::Scene;
  return fallback;
}

DesignMode otherDesignMode(DesignMode mode) noexcept {
  return mode == DesignMode::Emo ? DesignMode::Scene : DesignMode::Emo;
}

const DesignTokens& tokensFor(DesignMode mode, Contrast contrast) noexcept {
  static const std::array<DesignTokens, 4U> table{
      makeEmo(Contrast::Standard), makeEmo(Contrast::High),
      makeScene(Contrast::Standard), makeScene(Contrast::High)};
  const auto index = (mode == DesignMode::Scene ? 2U : 0U) +
                     (contrast == Contrast::High ? 1U : 0U);
  return table[index];
}

Color mix(Color a, Color b, double t) noexcept {
  const auto k = std::clamp(t, 0.0, 1.0);
  const auto lerp = [k](std::uint8_t x, std::uint8_t y) {
    return static_cast<std::uint8_t>(std::lround(x + (static_cast<double>(y) - x) * k));
  };
  return Color{lerp(a.red, b.red), lerp(a.green, b.green), lerp(a.blue, b.blue),
               lerp(a.alpha, b.alpha)};
}

double contrastRatio(Color foreground, Color background) noexcept {
  const auto channel = [](std::uint8_t value) {
    const auto v = static_cast<double>(value) / 255.0;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
  };
  const auto luminance = [&](Color c) {
    return 0.2126 * channel(c.red) + 0.7152 * channel(c.green) + 0.0722 * channel(c.blue);
  };
  const auto a = luminance(foreground);
  const auto b = luminance(background);
  return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

}  // namespace seam::native_ui::design
