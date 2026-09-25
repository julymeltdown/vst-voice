#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/application/performance_commands.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/rendering/singer_route.hpp"
#include "seam/synthesis/renderer_dispatcher.hpp"
#include "seam/synthesis/spectral_classic.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/voicebank/manifest_json.hpp"
#ifdef SEAM_FORMANT_CLAP
#include "seam/clap_editor/editor_runtime.hpp"
#endif

#include <cmath>
#include <numbers>
#include <chrono>
#include <future>
#include <thread>

namespace {
using namespace seam;
struct FormantFixture {
  application::ProjectFactory factory{991191U};
  domain::Project project{factory.createProject("Sample formants")};
  domain::TrackId track{factory.addVocalTrack(project, "Singer")};
  domain::RegionId region{factory.addRegion(project, track, "Phrase", time::Tick{137}, time::Tick{1920})};
  voicebank::AudioBuffer audio{.sampleRate = 48000U, .channels = 1U, .interleaved = std::vector<float>(24000U)};
  voicebank::Unit unit{test::support::makeUnit("a", {"a"}, "audio/a.wav", 45, voicebank::UnitKind::Sustain, 24000)};
  FormantFixture() {
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{1920}, 45U, U"あ", domain::Language::Japanese);
    note.phoneticHint = "a";
    project.findRegion(region)->lyrics = {lyric}; project.findRegion(region)->notes = {note};
    unit.renderer = voicebank::RendererHint::SpectralClassic;
    for (std::size_t i = 0; i < audio.interleaved.size(); ++i) {
      double sample = 0.0;
      for (unsigned harmonic = 1U; harmonic <= 100U; ++harmonic) {
        const double hz = 110.0 * harmonic;
        const auto bell = [hz](double center, double width) { const auto x = (hz - center) / width; return std::exp(-0.5 * x * x); };
        const auto magnitude = 0.01 + bell(700.0, 110.0) + 0.5 * bell(1400.0, 160.0) + 0.25 * bell(2600.0, 220.0);
        sample += magnitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / 48000.0);
      }
      audio.interleaved[i] = static_cast<float>(0.025 * sample);
    }
  }
  std::shared_ptr<const synthesis::CompiledScorePerformance> performance(const domain::Project& input) const {
    const auto p = synthesis::compileScorePerformance(input, *input.findRegion(region), 48000U); CHECK(p);
    return std::make_shared<const synthesis::CompiledScorePerformance>(p.value());
  }
  core::Result<synthesis::RenderedUnit> render(const domain::Project& input) const {
    const auto p = performance(input);
    synthesis::SpectralRenderParameters options;
    options.performance = p; options.performanceStartFrame = p->notes().front().startFrame;
    return synthesis::SpectralClassicRenderer{}.render(unit, audio, 48000U, 48000, 45, options);
  }
  domain::Project shifted(float amount) const {
    auto copy = project;
    CHECK(copy.findRegion(region)->formantAutomation.upsert({time::Tick{0}, amount}));
    return copy;
  }
};
struct SampleFormantFixture : FormantFixture {
  std::filesystem::path root{test::support::temporaryDirectory("spectral-formant")};
  voicebank::Manifest bank{test::support::makeManifest({unit})};
  SampleFormantFixture() {
    auto* singer = project.findVocalTrack(track);
    singer->voicebank = {bank.id, bank.version, std::string(64U, 'a')};
    singer->styleSelection = {domain::VoiceStyleOrigin::Explicit, "original"};
    std::filesystem::create_directories(root / "audio");
    CHECK(voicebank::writeMonoPcm16Wav(root / unit.audioPath, 48000U, audio.interleaved));
  }
  core::Result<rendering::RenderSnapshot> snapshot(const domain::Project& score,
      rendering::RenderQuality quality = rendering::RenderQuality::Final,
      const synthesis::PhraseRenderOptions& options = {}) const {
    const auto segments = rendering::PhraseSegmenter{}.segment(*score.findRegion(region)); CHECK(segments);
    return rendering::RenderSnapshotFactory{}.create(score, bank, track, segments.value().front(),
        1U, quality, root, 48000U, {}, options);
  }
  rendering::PhrasePipelineResult pcm(const rendering::RenderSnapshot& frozen) const {
    const auto result = rendering::PhraseRenderPipeline{}.render(frozen);
    if (!result) throw test::Failure{result.error().message + ": " + result.error().context};
    return result.value();
  }
};
double magnitude(std::span<const float> pcm, double frequency) {
  double real = 0, imaginary = 0;
  for (std::size_t i = 0; i < pcm.size(); ++i) {
    const auto window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(pcm.size() - 1U));
    const auto phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(i) / 48000.0;
    real += pcm[i] * window * std::cos(phase); imaginary += pcm[i] * window * std::sin(phase);
  }
  return std::hypot(real, imaginary);
}
double centroid(const std::vector<float>& pcm) {
  const auto window = std::span<const float>{pcm}.subspan(12000U, 12000U);
  double sum = 0, total = 0;
  for (unsigned harmonic = 2; harmonic <= 60; ++harmonic) {
    const auto hz = 110.0 * harmonic, value = magnitude(window, hz);
    sum += hz * value; total += value;
  }
  CHECK(total > 0); return sum / total;
}
}

TEST_CASE("spectral sample formants move the envelope independently and preserve exact neutral PCM") {
  FormantFixture f;
  const auto plain = f.render(f.project), zero = f.render(f.shifted(0.0F));
  CHECK(plain); CHECK(zero); CHECK(plain.value().samples == zero.value().samples);
  const auto base = centroid(plain.value().samples);
  const auto pitch = voicebank::analyzePitch(std::span<const float>{plain.value().samples}.subspan(12000U, 12000U), 48000U); CHECK(pitch);
  const auto basePitch = voicebank::medianVoicedPitch(pitch.value());
  for (const auto semitones : {-7.0F, 7.0F, 7.5F}) {
    const auto shifted = f.render(f.shifted(semitones));
    if (!shifted) throw test::Failure{shifted.error().message};
    CHECK(shifted.value().samples.size() == plain.value().samples.size());
    const auto center = centroid(shifted.value().samples);
    const auto p = voicebank::analyzePitch(std::span<const float>{shifted.value().samples}.subspan(12000U, 12000U), 48000U); CHECK(p);
    const auto hz = voicebank::medianVoicedPitch(p.value());
    std::cout << "[SPECTRAL-FORMANT] semitones=" << semitones << " centroid=" << center << " baseline=" << base
              << " f0=" << hz << " baselineF0=" << basePitch << '\n';
    CHECK(semitones > 0 ? center > base * 1.15 : center < base * 0.9);
    CHECK_NEAR(hz, basePitch, 2.0); CHECK_NEAR(hz, 110.0, 3.0);
    CHECK(std::all_of(shifted.value().samples.begin(), shifted.value().samples.end(), [](float x) { return std::isfinite(x) && std::abs(x) <= 1; }));
  }
}

TEST_CASE("spectral formant sub-hop islands change only their actual owned samples") {
  FormantFixture f;
  const auto original = f.render(f.project); CHECK(original);
  auto changed = f.project;
  auto& curve = changed.findRegion(f.region)->formantAutomation;
  // One tick = 25 frames at this clock. Each island is shorter than a hop,
  // including in the directly copied recorded onset and release.
  CHECK(curve.replacePoints({{time::Tick{0}, 0}, {time::Tick{7}, 0}, {time::Tick{8}, 7},
      {time::Tick{9}, 0}, {time::Tick{900}, 0}, {time::Tick{901}, -7.5F}, {time::Tick{902}, 0},
      {time::Tick{1820}, 0}, {time::Tick{1821}, 7.5F}, {time::Tick{1822}, 0}, {time::Tick{1920}, 0}}));
  const auto transformed = f.render(changed); CHECK(transformed);
  const auto p = f.performance(changed);
  std::array<std::size_t, 3> changes{};
  for (std::size_t i = 0; i < original.value().samples.size(); ++i) {
    const auto value = p->at(p->notes().front().startFrame + static_cast<time::SampleFrame>(i)).formantSemitones;
    if (value == 0) CHECK(original.value().samples[i] == transformed.value().samples[i]);
    else if (original.value().samples[i] != transformed.value().samples[i]) ++changes[i < 1000U ? 0U : i < 30000U ? 1U : 2U];
  }
  CHECK(std::all_of(changes.begin(), changes.end(), [](auto count) { return count > 0; }));
}

TEST_CASE("spectral formants survive normal preview final cache reload and transactional export") {
  SampleFormantFixture f;
  const auto neutral = f.snapshot(f.project); CHECK(neutral);
  const auto score = f.shifted(7.5F);
  const auto final = f.snapshot(score); CHECK(final);
  CHECK(final.value().contentHash != neutral.value().contentHash);
  CHECK(final.value().sample().renderOptions.renderer.controls.requiresControl(synthesis::RendererControl::Formant));
  const auto changed = f.pcm(final.value());
  CHECK(changed.rendered.audio.samples != f.pcm(neutral.value()).rendered.audio.samples);
  const auto preview = f.snapshot(score, rendering::RenderQuality::Preview); CHECK(preview);
  CHECK(f.pcm(preview.value()).rendered.audio.samples == changed.rendered.audio.samples);
  CHECK(formats::ProjectJsonCodec{}.save(score, f.root / "song.seam"));
  const auto loaded = formats::ProjectJsonCodec{}.load(f.root / "song.seam"); CHECK(loaded);
  const auto reloaded = f.snapshot(loaded.value()); CHECK(reloaded);
  CHECK(reloaded.value().contentHash == final.value().contentHash);
  CHECK(f.pcm(reloaded.value()).rendered.audio.samples == changed.rendered.audio.samples);
  const std::vector<rendering::TrackVoicebankSource> sources{{.trackId = f.track, .manifest = f.bank,
      .bankRoot = f.root, .contentHash = std::string(64U, 'a')}};
  rendering::PcmCache cache{f.root / "cache"};
  const auto render = [&] { return rendering::ProductionProjectRenderer{}.render(loaded.value(), sources,
      f.track, f.region, 1U, 48000U, rendering::RenderQuality::Final, {}, &cache); };
  const auto first = render(), cached = render(); CHECK(first); CHECK(cached);
  CHECK(first.value().diagnostics.empty()); CHECK(first.value().fallbackCount == 0U);
  CHECK(first.value().cacheHits == 0U); CHECK(cached.value().cacheHits > 0U);
  CHECK(first.value().interleaved == cached.value().interleaved);
  const auto exported = authoring::ExportService{}.exportProject(loaded.value(), sources, f.track, f.region,
      1U, f.root / "song.wav", voicebank::WavSampleFormat::Float32);
  CHECK(exported); CHECK(exported.value().state == authoring::ExportState::Committed);
  const auto wav = voicebank::readWav(exported.value().masterPath); CHECK(wav);
  CHECK(std::equal(wav.value().interleaved.begin(), wav.value().interleaved.end(),
      first.value().interleaved.begin(), first.value().interleaved.end()));
}

TEST_CASE("sample formant acceptance owns fractional islands with source offsets and neutral manual replacement") {
  using namespace domain; using time::Tick;
  SampleFormantFixture f;
  application::EditorSession session{f.project};
  const auto original = f.snapshot(session.project()); CHECK(original);
  const auto baseline = f.pcm(original.value()).rendered.audio;
  const auto& region = *session.project().findRegion(f.region);
  const auto pronunciation = phonemizer::resolvePronunciation(region); CHECK(pronunciation);
  PerformanceTake take{.id = "formants", .sourceRegionId = f.region,
      .capturedRevision = region.performance.revision,
      .resource = {SingerResourceKind::Sample, f.bank.id, f.bank.version, std::string(64U, 'a')},
      .pronunciation = pronunciation.value().identity, .generatorId = "fixture", .generatorVersion = "1",
      .range = {Tick{0}, Tick{1920}},
      .lanes = {{PerformanceChannel::Formant, {{Tick{700}, 7.5}, {Tick{704}, -7.5}}}}};
  const auto job = session.capturePerformanceJob(); CHECK(job);
  CHECK(session.executePerformanceResult(job.value(), std::make_unique<application::AddPerformanceProposalCommand>(
      f.region, region.performance, take)));
  CHECK(f.snapshot(session.project()).value().contentHash == original.value().contentHash);
  CHECK(session.execute(std::make_unique<application::SetAcceptedPerformanceCommand>(f.region,
      session.project().findRegion(f.region)->performance,
      std::vector<AcceptedPerformanceSelection>{{"formants", PerformanceChannel::Formant,
          PerformanceTimeRange{Tick{800}, Tick{804}}, Tick{-100}}})));
  const auto& accepted = session.project().findRegion(f.region)->performance;
  CHECK(session.execute(std::make_unique<application::EditPerformanceCommand>(
      std::vector<application::NoteExpressionEdit>{}, std::vector<application::RegionDynamicsEdit>{},
      std::vector<application::TrackStyleEdit>{}, std::vector<application::RegionOwnershipEdit>{{f.region,
          accepted.revision, accepted.ownership, {{PerformanceChannel::Formant,
          PerformanceTimeRange{Tick{801}, Tick{802}}, ManualPerformanceMode::Replace, {}}}}})));
  const auto frozen = f.snapshot(session.project()); CHECK(frozen);
  CHECK(frozen.value().compiledPerformance->requiresFormantControl());
  const auto audio = f.pcm(frozen.value()).rendered.audio;
  CHECK(audio.startFrame == baseline.startFrame); CHECK(audio.samples.size() == baseline.samples.size());
  std::size_t changed = 0U;
  for (std::size_t i = 0; i < audio.samples.size(); ++i) {
    const auto value = frozen.value().compiledPerformance->at(audio.startFrame + static_cast<time::SampleFrame>(i)).formantSemitones;
    if (value == 0.0F) CHECK(audio.samples[i] == baseline.samples[i]);
    else if (audio.samples[i] != baseline.samples[i]) ++changed;
  }
  CHECK(changed > 0U);
  const auto frame = [&](int tick) { return session.project().tempoMap().sampleFrameAt(Tick{137 + tick}, 48000U); };
  const synthesis::PhraseFrameRange window{frame(799), frame(805)};
  for (const auto block : {17U, 53U, 127U}) {
    const auto chunks = rendering::RenderSnapshotFactory{}.splitOwnedOutput(frozen.value(), window, block); CHECK(chunks);
    std::vector<float> joined;
    for (const auto& child : chunks.value()) {
      const auto pcm = f.pcm(child).rendered.audio.samples; joined.insert(joined.end(), pcm.begin(), pcm.end());
    }
    CHECK(std::equal(joined.begin(), joined.end(), audio.samples.begin() + (window.start - audio.startFrame)));
  }
  CHECK(session.undo()); CHECK(session.redo());
  CHECK(f.snapshot(session.project()).value().contentHash == frozen.value().contentHash);
  auto neutral = session.project();
  for (auto& point : neutral.findRegion(f.region)->performance.takes.front().lanes.front().points) point.value = 0.0;
  const auto zero = f.snapshot(neutral); CHECK(zero); CHECK(zero.value().compiledPerformance->requiresFormantControl());
  CHECK(f.pcm(zero.value()).rendered.audio.samples == baseline.samples);
  f.bank.units.front().renderer = voicebank::RendererHint::Raw;
  const auto refused = f.snapshot(neutral); CHECK(!refused);
  CHECK(refused.error().message.find("formant") != std::string::npos);
}

TEST_CASE("sample formant admission checks both styles overrides and direct dispatcher intent") {
  SampleFormantFixture f;
  const auto shifted = f.shifted(7.0F);
  const auto p = f.performance(shifted);
  synthesis::RendererDispatchParameters options;
  options.spectral.performance = p; options.spectral.performanceStartFrame = p->notes().front().startFrame;
  CHECK(synthesis::UnitRendererDispatcher{}.render(f.unit, f.audio, 48000U, 48000, 45, options));
  for (const auto renderer : {domain::UnitRendererKind::Raw, domain::UnitRendererKind::ClassicPsola,
                             domain::UnitRendererKind::Stretch}) {
    options.rendererOverride = renderer;
    const auto rejected = synthesis::UnitRendererDispatcher{}.render(f.unit, f.audio, 48000U, 48000, 45, options);
    CHECK(!rejected); CHECK(rejected.error().message.find("formant") != std::string::npos);
  }
  options.rendererOverride = domain::UnitRendererKind::SpectralClassic;
  options.spectral.performance.reset(); options.controls.require(synthesis::RendererControl::Formant);
  CHECK(!synthesis::UnitRendererDispatcher{}.render(f.unit, f.audio, 48000U, 48000, 45, options));
  auto second = f.bank.units.front(); second.id = "soft-a"; second.style = "soft";
  f.bank.units.push_back(second); f.bank.styles.push_back("soft");
  auto pair = shifted;
  pair.findVocalTrack(f.track)->styleSelection.blend = domain::VoiceStyleBlend{"soft", 0.5F};
  const auto both = f.snapshot(pair); CHECK(both); CHECK(both.value().sample().blendStyle);
  CHECK(f.pcm(both.value()).rendered.audio.samples == f.pcm(f.snapshot(shifted).value()).rendered.audio.samples);
  f.bank.units.back().renderer = voicebank::RendererHint::Raw;
  const auto wrongSecond = f.snapshot(pair); CHECK(!wrongSecond);
  CHECK(wrongSecond.error().context == "soft-a");
  auto forced = shifted;
  const auto& key = both.value().phonemes->tokens.front().key;
  forced.findRegion(f.region)->unitSelectionOverrides.push_back({.startKey = key, .unitId = "a", .renderer = domain::UnitRendererKind::Raw});
  const auto wrongOverride = f.snapshot(forced); CHECK(!wrongOverride);
  CHECK(wrongOverride.error().message.find("formant") != std::string::npos);
  f.bank.units.front().renderer = voicebank::RendererHint::ClassicPsola;
  f.bank.units.front().pitchMarks = {{8000, 1.0F}, {8436, 1.0F}, {8873, 1.0F}};
  forced.findRegion(f.region)->unitSelectionOverrides.front().renderer = domain::UnitRendererKind::SpectralClassic;
  const auto explicitSpectral = f.snapshot(forced);
  if (!explicitSpectral) throw test::Failure{explicitSpectral.error().message};
  CHECK(f.pcm(explicitSpectral.value()).rendered.audio.samples.size() > 0U); // Explicit renderer wins over the bank hint.
}

TEST_CASE("spectral formant silence extrema unvoiced material gain and budgets fail honestly") {
  FormantFixture f;
  for (const auto amount : {-24.0F, 24.0F, -0.5F, 0.5F}) {
    const auto saved = f.audio.interleaved;
    for (auto& sample : f.audio.interleaved) sample *= 0.05F;
    const auto rendered = f.render(f.shifted(amount)); CHECK(rendered);
    for (const auto sample : rendered.value().samples) CHECK(std::isfinite(sample) && std::abs(sample) <= 1.0F);
    std::fill(f.audio.interleaved.begin(), f.audio.interleaved.end(), 0.0F);
    const auto silent = f.render(f.shifted(amount)); CHECK(silent);
    CHECK(std::all_of(silent.value().samples.begin(), silent.value().samples.end(), [](float x) { return x == 0.0F; }));
    f.audio.interleaved = saved;
  }
  auto performance = f.performance(f.shifted(7.0F));
  synthesis::SpectralRenderParameters options; options.performance = performance;
  options.performanceStartFrame = performance->notes().front().startFrame;
  const auto start = options.performanceStartFrame;
  options.sourceMap = synthesis::SourceTargetMap{.knots = {{0, start}, {24000, start + 48000}},
      .voicing = {{0, 24000, false}}};
  const auto unvoiced = synthesis::SpectralClassicRenderer{}.render(f.unit, f.audio, 48000U, 48000, 45, options); CHECK(unvoiced);
  options.performance = f.performance(f.project);
  const auto neutralUv = synthesis::SpectralClassicRenderer{}.render(f.unit, f.audio, 48000U, 48000, 45, options); CHECK(neutralUv);
  CHECK(unvoiced.value().samples != neutralUv.value().samples);
  options.sourceMap.reset(); options.performance = performance; options.additionalGainDb = 60.0F;
  const auto excessive = synthesis::SpectralClassicRenderer{}.render(f.unit, f.audio, 48000U, 48000, 45, options);
  CHECK(!excessive); CHECK(excessive.error().message.find("headroom") != std::string::npos);
  options.additionalGainDb = 0.0F; options.fftSize = 8192U; options.hopSize = 1U;
  const auto scan = synthesis::SpectralClassicRenderer{}.render(f.unit, f.audio, 48000U, 48000, 45, options);
  CHECK(!scan); CHECK(scan.error().message.find("control scan") != std::string::npos);
  options.hopSize = 256U;
  auto sweep = f.project;
  std::vector<domain::FormantAutomationPoint> points;
  for (int tick = 0; tick <= 1920; tick += 32) points.push_back({time::Tick{tick}, tick % 64 == 0 ? -24.0F : 24.0F});
  CHECK(sweep.findRegion(f.region)->formantAutomation.replacePoints(points));
  options.performance = f.performance(sweep);
  const auto work = synthesis::SpectralClassicRenderer{}.render(f.unit, f.audio, 48000U, 48000, 45, options);
  CHECK(!work); CHECK(work.error().message.find("FFT work") != std::string::npos);
  options.fftSize = 1024U; options.hopSize = 256U;
  std::stop_source stop;
  auto running = std::async(std::launch::async, [&] {
    return synthesis::SpectralClassicRenderer{}.render(f.unit, f.audio, 48000U, 480000, 45, options, stop.get_token());
  });
  std::this_thread::sleep_for(std::chrono::milliseconds{5});
  const auto cancellation = std::chrono::steady_clock::now(); stop.request_stop();
  CHECK(running.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
  const auto cancelled = running.get(); CHECK(!cancelled); CHECK(cancelled.error().code == core::ErrorCode::Conflict);
  std::cout << "[SPECTRAL-FORMANT] cancellationMs=" << std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - cancellation).count() << '\n';
}

TEST_CASE("native formant editing uses resolved sample capabilities and stays undoable") {
  SampleFormantFixture f;
  application::EditorSession session{f.project};
  const auto validate = [&](domain::TrackId trackId, synthesis::RendererControl control) -> core::Result<void> {
    const auto* track = session.project().findVocalTrack(trackId); CHECK(track);
    const auto environment = rendering::sampleSingerRouteEnvironment(*track, f.bank);
    const auto route = rendering::resolveSingerRoute(session.project(), trackId, environment); CHECK(route);
    return rendering::validateRouteControl(route.value(), control);
  };
  native_ui::EditorHostCallbacks callbacks;
  callbacks.validateSingerControl = validate;
  native_ui::NativeEditorController controller{session, f.factory, f.region, callbacks};
  // Edits "at the playhead" need the playhead inside the region, which starts at tick 137.
  controller.setPlayheadTick(time::Tick{137});
  CHECK(controller.nudgeFormantShift(7));
  const auto authored = session.project();
  CHECK(f.snapshot(authored));
  CHECK(session.undo()); CHECK(session.project() == f.project);
  CHECK(session.redo()); CHECK(session.project() == authored);
  auto raw = f.bank.units.front(); raw.id = "raw-a"; raw.renderer = voicebank::RendererHint::Raw;
  f.bank.units.push_back(raw);
  CHECK(!controller.nudgeFormantShift(1)); CHECK(session.project() == authored);
  f.bank.units.back().enabled = false;
  CHECK(validate(f.track, synthesis::RendererControl::Formant));
  CHECK(!validate(f.track, synthesis::RendererControl::Breathiness));
  // Without a resolved inventory the family does not advertise sample formants.
  CHECK(!rendering::resolveSingerRoute(session.project(), f.track).value().supportsControl(synthesis::RendererControl::Formant));
  session.project().findVocalTrack(f.track)->styleSelection.blend = domain::VoiceStyleBlend{"absent", 0.5F};
  CHECK(!validate(f.track, synthesis::RendererControl::Formant));
}

TEST_CASE("spectral formant gain is applied once and bounded geometry remains finite") {
  FormantFixture f;
  auto score = f.shifted(7.5F);
  const auto full = f.render(score); CHECK(full);
  CHECK(score.findRegion(f.region)->dynamicsAutomation.upsert({time::Tick{0}, 0.5F}));
  const auto half = f.render(score); CHECK(half);
  for (std::size_t i = 0; i < full.value().samples.size(); ++i)
    CHECK_NEAR(half.value().samples[i], full.value().samples[i] * 0.5F, 1.0e-7);
  CHECK(score.findRegion(f.region)->dynamicsAutomation.upsert({time::Tick{0}, 1.0F}));
  for (const auto rate : {8000U, 384000U}) {
    for (const auto fftSize : {128U, 8192U}) {
      const auto compiled = synthesis::compileScorePerformance(score, *score.findRegion(f.region), rate); CHECK(compiled);
      synthesis::SpectralRenderParameters options;
      options.performance = std::make_shared<const synthesis::CompiledScorePerformance>(compiled.value());
      options.performanceStartFrame = options.performance->notes().front().startFrame;
      options.fftSize = fftSize; options.hopSize = fftSize / 2U;
      auto source = f.audio;
      for (auto& sample : source.interleaved) sample *= 0.01F;
      const auto started = std::chrono::steady_clock::now();
      const auto rendered = synthesis::SpectralClassicRenderer{}.render(f.unit, source, rate, 8192, 45, options);
      CHECK(rendered);
      CHECK(std::all_of(rendered.value().samples.begin(), rendered.value().samples.end(),
          [](float value) { return std::isfinite(value) && std::abs(value) <= 1.0F; }));
      std::cout << "[SPECTRAL-FORMANT] rate=" << rate << " fft=" << fftSize << " frames=8192 elapsedMs="
                << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() << '\n';
    }
  }
  f.audio.interleaved.front() = std::numeric_limits<float>::quiet_NaN();
  const auto invalid = f.render(score); CHECK(!invalid); CHECK(invalid.error().message == "Nonfinite formant source");
}

#ifdef SEAM_FORMANT_CLAP
TEST_CASE("CLAP sample formant edit reaches a current preview and fixed-audio offline bounce") {
  SampleFormantFixture f;
  CHECK(voicebank::ManifestJsonCodec{}.save(f.bank, f.root / "manifest.json"));
  const std::vector<voicebank::VoicebankSearchRoot> roots{{f.root, voicebank::VoicebankRootKind::Development}};
  const auto scanned = voicebank::VoicebankCatalog{}.scan(roots); CHECK(scanned); CHECK(scanned.value().size() == 1U);
  f.project.findVocalTrack(f.track)->voicebank.contentHash = scanned.value().front().contentHash;
  clap_editor::EditorRuntime runtime{f.project, {}, roots};
  const auto ready = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
    while (std::chrono::steady_clock::now() < deadline) {
      const auto preview = runtime.renderedPreview();
      if (preview && preview->revision == runtime.revision()) {
        if (preview->status == clap_editor::PreviewStatus::Ready) return preview;
        if (preview->status == clap_editor::PreviewStatus::Failed) throw test::Failure{preview->diagnostic};
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    throw test::Failure{"CLAP formant preview did not publish the current revision"};
  };
  const auto before = ready();
  runtime.controller().setPlayheadTick(time::Tick{137});
  CHECK(runtime.controller().nudgeFormantShift(7));
  const auto changed = ready();
  CHECK(changed->revision > before->revision); CHECK(changed->fallbackCount == 0U);
  CHECK(changed->interleaved != before->interleaved);
  CHECK(runtime.prepareOfflineRender(std::chrono::seconds{15}));
  CHECK(runtime.offlineRenderReady());
  const auto bounced = runtime.acquireOfflineRenderedPreview(); CHECK(bounced);
  CHECK(bounced->interleaved == changed->interleaved);
}
#endif
