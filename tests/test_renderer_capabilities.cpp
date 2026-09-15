#include "test_framework.hpp"

#include "seam/synthesis/renderer_capabilities.hpp"
#include "seam/synthesis/renderer_dispatcher.hpp"
#include "seam/domain/project.hpp"

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

// The six timbral channels are the ones whose availability differs by carrier, and the pair of
// mistakes this test exists to prevent is a carrier that grants a control its renderer refuses and a
// carrier that refuses one it actually applies. A source-filter track is not a sample bank, and a
// neural track is neither, so each is asserted independently rather than inferred from "not sample".
TEST_CASE("each carrier resolves exactly the controls its renderer applies") {
  using namespace seam::synthesis;
  const std::array<RendererControl, 13U> all{
      RendererControl::Pitch,       RendererControl::Timing,      RendererControl::Dynamics,
      RendererControl::Vibrato,     RendererControl::Attack,      RendererControl::Release,
      RendererControl::Formant,     RendererControl::Breathiness, RendererControl::Tension,
      RendererControl::Airiness,    RendererControl::Gender,      RendererControl::StyleBlend,
      RendererControl::Growl};
  const std::array<RendererControl, 6U> timbral{
      RendererControl::Formant, RendererControl::Breathiness, RendererControl::Tension,
      RendererControl::Airiness, RendererControl::Gender, RendererControl::Growl};
  const auto sample = rendererCapabilities(RendererCarrier::SampleBank);
  const auto sourceFilter = rendererCapabilities(RendererCarrier::SourceFilter);
  const auto neural = rendererCapabilities(RendererCarrier::Neural);
  for (const auto control : timbral) {
    CHECK(!sample.supports(control));
    CHECK(sourceFilter.supports(control));
    // A learned singer receives audio; it does not own the excitation or tract those channels move.
    CHECK(!neural.supports(control));
  }
  for (const auto control : all) {
    // Every carrier consumes the shared compiled performance, so the non-timbral controls are common
    // to all three. StyleBlend is declared nowhere as an implemented interpolation.
    const bool shared = control != RendererControl::StyleBlend &&
                        std::none_of(timbral.begin(), timbral.end(),
                                     [control](RendererControl candidate) { return candidate == control; });
    if (!shared) continue;
    CHECK(sample.supports(control));
    CHECK(sourceFilter.supports(control));
    CHECK(neural.supports(control));
  }
  // Neural is not silently aliased to the sample carrier, even though both refuse the six channels.
  CHECK(!sample.supports(RendererControl::StyleBlend));
  CHECK(!neural.supports(RendererControl::StyleBlend));
}

TEST_CASE("a track resolves the carrier its recorded singer will render") {
  using namespace seam::synthesis;
  namespace domain = seam::domain;
  domain::VocalTrack track{};
  CHECK(rendererCarrierFor(track) == RendererCarrier::SampleBank);
  track.proceduralRecipe = domain::ProceduralRecipeReference{
      .resource = domain::SingerResourceIdentity{.kind = domain::SingerResourceKind::Procedural,
                                                 .id = "preset",
                                                 .version = "1.0.0",
                                                 .contentHash = std::string(64U, 'a')},
      .path = "recipe.json",
      .style = "neutral"};
  CHECK(rendererCarrierFor(track) == RendererCarrier::SourceFilter);
  // A track selects one singer family, so a neural selection replaces the procedural carrier rather
  // than leaving the source-filter controls advertised under a neural singer.
  track.proceduralRecipe.reset();
  track.neuralResource = domain::NeuralResourceReference{
      .resource = domain::SingerResourceIdentity{.kind = domain::SingerResourceKind::Neural,
                                                 .id = "model",
                                                 .version = "1.0.0",
                                                 .contentHash = std::string(64U, 'b')}};
  CHECK(rendererCarrierFor(track) == RendererCarrier::Neural);
  // And the neural track refuses the six timbral channels through the same validator the surface and
  // the renderer use, so the decision cannot be advertised by one and rejected by another.
  for (const auto control : {RendererControl::Formant, RendererControl::Breathiness,
                             RendererControl::Tension, RendererControl::Airiness,
                             RendererControl::Gender, RendererControl::Growl}) {
    RendererControlRequest request;
    request.require(control);
    const auto refused = validateRendererCapabilities(RendererCarrier::Neural, request);
    CHECK(!refused.hasValue());
    CHECK(refused.error().message.find(std::string{rendererControlName(control)}) != std::string::npos);
  }
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
