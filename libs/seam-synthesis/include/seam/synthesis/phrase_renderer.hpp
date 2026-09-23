#pragma once

#include "seam/core/result.hpp"
#include "seam/synthesis/raw_renderer.hpp"
#include "seam/synthesis/source_phoneme_alignment.hpp"
#include "seam/synthesis/renderer_dispatcher.hpp"
#include "seam/synthesis/seam_composer.hpp"
#include "seam/synthesis/timing_solver.hpp"
#include "seam/voicebank/acoustic_analysis.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace seam::synthesis {

struct RenderedPlacementInfo final {
  std::string unitId;
  time::SampleFrame requestedStart{0};
  time::SampleFrame alignedStart{0};
  time::SampleFrame frameCount{0};
  time::SampleFrame vowelOnset{0};
  voicebank::RendererHint requestedRenderer{voicebank::RendererHint::Raw};
  voicebank::RendererHint actualRenderer{voicebank::RendererHint::Raw};
  bool usedFallback{false};
  bool forcedSelection{false};
  float seamAmount{0.7F};
  domain::SeamCurve seamCurve{domain::SeamCurve::HardCharacter};
  std::string diagnostic;
  // Populated by the ordered pair composer so two arms never masquerade as
  // one unit sequence in inspection/export diagnostics.
  std::string style{};
};

struct PhraseRenderResult final {
  PhraseAudio audio;
  std::vector<RenderedPlacementInfo> placements;
};

class RawPhraseRenderer final {
public:
  [[nodiscard]] core::Result<PhraseRenderResult> render(
      const voicebank::Manifest& manifest,
      const std::filesystem::path& bankRoot,
      const TimingPlan& timing,
      std::uint32_t outputSampleRate,
      RawRenderParameters renderParameters = {},
      SeamSettings seamSettings = {}) const;
};

struct PhraseRenderOptions final {
  RendererDispatchParameters renderer{};
  SeamSettings defaultSeam{};
};

struct FrozenUnitAudio final {
  std::string unitId;
  std::shared_ptr<const voicebank::AudioBuffer> audio;
  std::optional<SourcePhonemeAlignment> sourceAlignment{};
  std::string verifiedAudioSha256{};
  // Measured voicing for this exact take, when a stored analysis was supplied.
  // Absent means nothing measured this take's voicing, so consumers must treat
  // voicing as unknown rather than assuming either answer.
  std::optional<voicebank::AcousticAnalysis> acousticAnalysis{};
};

class ConcatenativePhraseRenderer final {
public:
  [[nodiscard]] core::Result<PhraseRenderResult> render(
      const voicebank::Manifest& manifest,
      const std::filesystem::path& bankRoot,
      const domain::Project& project,
      const domain::VocalRegion& region,
      const UnitPlan& unitPlan,
      const TimingPlan& timing,
      std::uint32_t outputSampleRate,
      const PhraseRenderOptions& options = {},
      std::stop_token stopToken = {}) const;

  // Snapshot-safe overload. Every selected Unit must have a frozen decoded
  // audio buffer produced from the exact bytes used by the render identity.
  [[nodiscard]] core::Result<PhraseRenderResult> render(
      const voicebank::Manifest& manifest,
      const domain::Project& project,
      const domain::VocalRegion& region,
      const UnitPlan& unitPlan,
      const TimingPlan& timing,
      std::uint32_t outputSampleRate,
      const PhraseRenderOptions& options,
      std::span<const FrozenUnitAudio> frozenAudio,
      std::stop_token stopToken = {}) const;
};

}  // namespace seam::synthesis
