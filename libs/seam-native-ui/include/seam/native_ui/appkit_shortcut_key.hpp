#pragma once

#include "seam/native_ui/editor_controller.hpp"

namespace seam::native_ui {

// Fallback for non-Latin event characters outside text input. ASCII layout
// mappings remain character-based. Codes are kVK_ANSI_* from HIToolbox Events.h.
[[nodiscard]] constexpr NativeKey appKitNonLatinShortcutKey(
    std::uint16_t keyCode, char32_t character) noexcept {
  if (character < 128U) return NativeKey::Unknown;
  switch (keyCode) {
    case 0x00U: return NativeKey::A;
    case 0x01U: return NativeKey::S;
    case 0x02U: return NativeKey::D;
    case 0x06U: return NativeKey::Z;
    case 0x07U: return NativeKey::X;
    case 0x08U: return NativeKey::C;
    case 0x09U: return NativeKey::V;
    case 0x0BU: return NativeKey::B;
    case 0x0CU: return NativeKey::Q;
    case 0x0EU: return NativeKey::E;
    case 0x0FU: return NativeKey::R;
    case 0x10U: return NativeKey::Y;
    case 0x18U: return NativeKey::Plus;
    case 0x1BU: return NativeKey::Minus;
    case 0x1FU: return NativeKey::O;
    case 0x22U: return NativeKey::I;
    case 0x23U: return NativeKey::P;
    case 0x25U: return NativeKey::L;
    case 0x2DU: return NativeKey::N;
    default: return NativeKey::Unknown;
  }
}

} // namespace seam::native_ui
