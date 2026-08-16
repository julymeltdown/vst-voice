#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/render_controls.hpp"
#include "seam/synthesis/classic_psola.hpp"
#include "seam/synthesis/raw_renderer.hpp"
#include "seam/synthesis/spectral_classic.hpp"
#include "seam/synthesis/stretch_renderer.hpp"
#include "seam/voicebank/voicebank.hpp"
#include "seam/voicebank/wav.hpp"

#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>

namespace seam::synthesis {

enum class RenderPolicy {
  RespectVoicebank,
  ForceRaw,
  ForceClassicPsola,
  ForceSpectralClassic,
  ForceStretch,
};

struct RendererDispatchParameters final {
  RenderPolicy policy{RenderPolicy::RespectVoicebank};
  bool allowRawFallback{true};
  std::optional<domain::UnitRendererKind> rendererOverride;
  RawRenderParameters raw{};
  PsolaRenderParameters psola{};
  SpectralRenderParameters spectral{};
  StretchRenderParameters stretch{};
};

struct DispatchedRenderedUnit final {
  RenderedUnit unit;
  voicebank::RendererHint requested{voicebank::RendererHint::Raw};
  voicebank::RendererHint actual{voicebank::RendererHint::Raw};
  bool usedFallback{false};
  std::string diagnostic;
};

class UnitRendererDispatcher final {
public:
  [[nodiscard]] core::Result<DispatchedRenderedUnit> render(
      const voicebank::Unit& unit,
      const voicebank::AudioBuffer& source,
      std::uint32_t outputSampleRate,
      time::SampleFrame outputFrames,
      std::int32_t targetMidi,
      const RendererDispatchParameters& parameters = {},
      std::stop_token stopToken = {}) const;
};

}  // namespace seam::synthesis
