#include "test_framework.hpp"

#include "seam/synthesis/renderer_capabilities.hpp"
#include "seam/synthesis/renderer_dispatcher.hpp"

#include "test_support.hpp"

#include <string>

TEST_CASE("renderer capabilities expose only controls implemented by current backends") {
  using namespace seam::synthesis;
  const auto raw = rendererCapabilities(seam::voicebank::RendererHint::Raw);
  const auto psola = rendererCapabilities(
      seam::voicebank::RendererHint::ClassicPsola);
  CHECK(raw.supports(RendererControl::Pitch));
  CHECK(raw.supports(RendererControl::Dynamics));
  CHECK(psola.supports(RendererControl::Vibrato));
  CHECK(psola.pitchPreservingTransient);
  CHECK(!raw.pitchPreservingTransient);
  CHECK(!psola.supports(RendererControl::Formant));
  CHECK(rendererControlName(RendererControl::StyleBlend) == "style-blend");
}

TEST_CASE("required unsupported controls fail before a renderer can hide them") {
  using namespace seam::synthesis;
  RendererControlRequest request;
  request.require(RendererControl::Formant);
  const auto rejected = validateRendererCapabilities(
      seam::voicebank::RendererHint::SpectralClassic, request, true);
  CHECK(!rejected);
  CHECK(rejected.error().code == seam::core::ErrorCode::Unsupported);
  CHECK(rejected.error().message.find("formant") != std::string::npos);

  RendererControlRequest transient;
  transient.require(RendererControl::Pitch);
  transient.requiresPitchPreservingTransient = true;
  const auto rawRejected = validateRendererCapabilities(
      seam::voicebank::RendererHint::Raw, transient, true);
  CHECK(!rawRejected);
  CHECK(rawRejected.error().message.find("transient") != std::string::npos);
}

TEST_CASE("dispatcher rejects an unsupported required control without rendering source audio") {
  using namespace seam::synthesis;
  RendererControlRequest request;
  request.require(RendererControl::Growl);
  RendererDispatchParameters parameters{
      .policy = RenderPolicy::ForceClassicPsola,
      .allowRawFallback = true,
      .rendererOverride = std::nullopt,
      .raw = {},
      .psola = {},
      .spectral = {},
      .stretch = {},
      .controls = request,
  };
  const auto result = UnitRendererDispatcher{}.render(
      seam::test::support::makeUnit("capability", {"a"}, "a.wav"),
      seam::voicebank::AudioBuffer{}, 48000U, 128, 60, parameters);
  CHECK(!result);
  CHECK(result.error().code == seam::core::ErrorCode::Unsupported);
}

TEST_CASE("raw renderer applies an explicit pitch trajectory instead of dropping it") {
  using namespace seam::synthesis;
  constexpr std::uint32_t sampleRate = 48000U;
  const auto samples = seam::test::support::sineWave(sampleRate, 220.0, 0.5);
  const seam::voicebank::AudioBuffer source{
      .sampleRate = sampleRate, .channels = 1U, .interleaved = samples};
  const auto unit = seam::test::support::makeUnit(
      "trajectory", {"a"}, "trajectory.wav", 69,
      seam::voicebank::UnitKind::Cv, samples.size());
  const auto baseline = RawLoopRenderer{}.render(
      unit, source, sampleRate, 16000, 69, RawRenderParameters{});
  RawRenderParameters pitched;
  pitched.pitchCurve = PitchCurve{
      {{0, 0.0F, seam::domain::CurveInterpolation::Linear},
       {8000, 1200.0F, seam::domain::CurveInterpolation::Linear},
       {15999, 1200.0F, seam::domain::CurveInterpolation::Linear}}};
  const auto changed = RawLoopRenderer{}.render(
      unit, source, sampleRate, 16000, 69, pitched);
  CHECK(baseline);
  CHECK(changed);
  CHECK(baseline.value().samples.size() == changed.value().samples.size());
  double difference = 0.0;
  for (std::size_t index = 4800U; index < 15000U; ++index) {
    difference += std::abs(static_cast<double>(baseline.value().samples[index]) -
                           static_cast<double>(changed.value().samples[index]));
  }
  CHECK(difference > 1.0);
}
