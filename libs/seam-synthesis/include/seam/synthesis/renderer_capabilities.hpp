#pragma once

#include "seam/core/result.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace seam::synthesis {

// These are render-time capabilities, not UI labels. A control marked
// required must either be applied by the selected backend or cause an
// explicit failure; it may not disappear behind a generic Raw fallback.
enum class RendererControl {
  Pitch,
  Timing,
  Dynamics,
  Vibrato,
  Attack,
  Release,
  Formant,
  Breathiness,
  Tension,
  Airiness,
  Gender,
  StyleBlend,
  Growl,
};

inline constexpr std::size_t kRendererControlCount = 13U;

struct RendererControlRequest final {
  std::array<bool, kRendererControlCount> required{};
  // Source-aligned transient remapping requires a pitch-preserving backend.
  bool requiresPitchPreservingTransient{false};

  [[nodiscard]] bool requiresControl(RendererControl control) const noexcept {
    return required[static_cast<std::size_t>(control)];
  }
  void require(RendererControl control) noexcept {
    required[static_cast<std::size_t>(control)] = true;
  }
};

struct RendererCapabilityView final {
  voicebank::RendererHint renderer{voicebank::RendererHint::Raw};
  std::array<bool, kRendererControlCount> supported{};
  bool pitchPreservingTransient{false};

  [[nodiscard]] bool supports(RendererControl control) const noexcept {
    return supported[static_cast<std::size_t>(control)];
  }
};

struct RendererCapabilityDecision final {
  RendererCapabilityView requested;
  RendererCapabilityView fallback;
  bool canFallbackToRaw{false};
  std::string diagnostic;
};

// Which execution carrier a decision is about. A sample bank and the source-filter engine are
// different carriers with different controls, so a request is answered for the carrier that will
// actually run instead of for a hint the carrier does not have. The formant channel is the first
// control where the two answers differ: the source-filter engine moves its own resonances, and a
// concatenative bank cannot.
enum class RendererCarrier { SampleBank, SourceFilter };

[[nodiscard]] RendererCapabilityView rendererCapabilities(
    voicebank::RendererHint renderer) noexcept;
[[nodiscard]] RendererCapabilityView rendererCapabilities(
    RendererCarrier carrier) noexcept;
[[nodiscard]] core::Result<RendererCapabilityDecision>
validateRendererCapabilities(voicebank::RendererHint renderer,
                             const RendererControlRequest& request,
                             bool allowRawFallback);
[[nodiscard]] core::Result<RendererCapabilityDecision>
validateRendererCapabilities(RendererCarrier carrier,
                             const RendererControlRequest& request,
                             bool allowRawFallback = false);
[[nodiscard]] std::string_view rendererControlName(
    RendererControl control) noexcept;

}  // namespace seam::synthesis
