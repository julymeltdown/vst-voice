#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/application/performance_commands.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/rendering/style_blend.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/rendering/region_renderer.hpp"
#include "seam/rendering/project_renderer.hpp"
#include "seam/rendering/singer_route.hpp"

#include <cmath>
#include <numbers>
#include <limits>

namespace {
using namespace seam;
struct Fixture {
  application::ProjectFactory factory{989100U};
  domain::Project project{factory.createProject("Paired styles")};
  domain::TrackId track{factory.addVocalTrack(project, "Singer")};
  domain::RegionId region{factory.addRegion(project, track, "Phrase", time::Tick{960}, time::Tick{1920})};
  domain::NoteId note;
  std::filesystem::path root{test::support::temporaryDirectory("style-pair")};
  voicebank::Manifest bank;
  Fixture() {
    auto primary = test::support::makeUnit("a", {"a"}, "audio/a.wav", 69,
        voicebank::UnitKind::Sustain, 24000);
    primary.renderer = voicebank::RendererHint::ClassicPsola;
    for (std::size_t pulse = 1U; pulse < 220U; ++pulse) {
      const auto frame = static_cast<time::SampleFrame>(std::llround(static_cast<double>(pulse) * 48000.0 / 440.0));
      if (frame < 24000) primary.pitchMarks.push_back({frame, 1.0F});
    }
    auto secondary = primary; secondary.id = "soft-a"; secondary.style = "soft"; secondary.audioPath = "audio/b.wav";
    bank = test::support::makeManifest({primary, secondary}); bank.styles = {"original", "soft"};
    auto* singer = project.findVocalTrack(track);
    singer->voicebank = {bank.id, bank.version, std::string(64U, 'a')};
    singer->styleSelection = {domain::VoiceStyleOrigin::Explicit, "original", domain::VoiceStyleBlend{"soft", 0.5F}};
    auto [lyric, value] = factory.makeNote(time::Tick{480}, time::Tick{960}, 69U, U"あ", domain::Language::Japanese);
    value.phoneticHint = "a"; note = value.id;
    project.findRegion(region)->lyrics = {lyric}; project.findRegion(region)->notes = {value};
    std::filesystem::create_directories(root / "audio");
    auto a = test::support::sineWave(48000U, 440.0, 0.5);
    auto b = a;
    for (std::size_t i = 0; i < b.size(); ++i) b[i] = static_cast<float>(
        0.7 * static_cast<double>(a[i]) + 0.1 * std::sin(2.0 * std::numbers::pi * 880.0 * static_cast<double>(i) / 48000.0));
    CHECK(voicebank::writeMonoPcm16Wav(root / "audio/a.wav", 48000U, a));
    CHECK(voicebank::writeMonoPcm16Wav(root / "audio/b.wav", 48000U, b));
  }
  core::Result<rendering::RenderSnapshot> snapshot(const domain::Project& source) const {
    const auto segments = rendering::PhraseSegmenter{}.segment(*source.findRegion(region)); CHECK(segments);
    return rendering::RenderSnapshotFactory{}.create(source, bank, track, segments.value().front(), 1U,
        rendering::RenderQuality::Final, root, 48000U);
  }
  rendering::PhrasePipelineResult render(const domain::Project& source) const {
    auto frozen = snapshot(source);
    if (!frozen) throw std::runtime_error(frozen.error().message + ": " + frozen.error().context);
    auto result = rendering::PhraseRenderPipeline{}.render(frozen.value());
    if (!result) throw std::runtime_error(result.error().message + ": " + result.error().context);
    return std::move(result).value();
  }
  synthesis::CompiledScorePerformance performance(float amount) {
    project.findVocalTrack(track)->styleSelection.blend->amount = amount;
    const auto result = synthesis::compileScorePerformance(project, *project.findRegion(region), 48000U); CHECK(result);
    return result.value();
  }
};
}

TEST_CASE("paired PCM endpoints reproduce standalone styles and a midpoint preserves pitch") {
  Fixture f;
  for (const auto amount : {0.0F, 1.0F}) {
    auto paired = f.project;
    paired.findVocalTrack(f.track)->styleSelection.blend->amount = amount;
    auto single = paired;
    single.findVocalTrack(f.track)->styleSelection = {domain::VoiceStyleOrigin::Explicit, amount == 0.0F ? "original" : "soft"};
    const auto actual = f.render(paired), expected = f.render(single);
    CHECK(actual.rendered.audio.startFrame == expected.rendered.audio.startFrame);
    CHECK(actual.rendered.audio.samples == expected.rendered.audio.samples);
    CHECK(actual.secondaryTiming); CHECK(actual.secondaryUnitPlan); CHECK(actual.styleBlendCompatibility);
    CHECK(actual.rendered.placements.size() == 2U);
    CHECK(actual.rendered.placements[0].style == "original");
    CHECK(actual.rendered.placements[1].style == "soft");
  }
  auto a = f.project, b = f.project;
  a.findVocalTrack(f.track)->styleSelection.blend->amount = 0.0F;
  b.findVocalTrack(f.track)->styleSelection.blend->amount = 1.0F;
  const auto first = f.render(a), second = f.render(b), middle = f.render(f.project);
  CHECK(middle.rendered.audio.samples != first.rendered.audio.samples);
  CHECK(middle.rendered.audio.samples != second.rendered.audio.samples);
  const auto& pcm = middle.rendered.audio.samples; CHECK(pcm.size() > 18000U);
  const auto pitch = voicebank::analyzePitch(std::span<const float>{pcm.data() + 8000U, 8000U}, 48000U); CHECK(pitch);
  CHECK(std::abs(voicebank::medianVoicedPitch(pitch.value()) - 440.0) < 3.0);
}

TEST_CASE("paired PCM compatibility distinguishes identical opposite delayed drifting and silent arms") {
  Fixture f;
  synthesis::PhraseAudio a{.startFrame = 35000, .samples = test::support::sineWave(48000U, 400.0, 0.2)};
  auto b = a;
  auto performance = f.performance(0.5F);
  auto same = rendering::crossfadeStylePair(a, b, performance); CHECK(same);
  CHECK(same.value().audio.samples == a.samples); CHECK(same.value().compatibility.comparedWindows > 0U);
  for (auto& sample : b.samples) sample = -sample;
  CHECK(!rendering::crossfadeStylePair(a, b, performance));
  // A half-period delay has the same destructive outcome; no corrective shift.
  b = a; b.startFrame += 60;
  CHECK(!rendering::crossfadeStylePair(a, b, performance));
  b = a; b.samples = test::support::sineWave(48000U, 405.0, 0.2);
  CHECK(!rendering::crossfadeStylePair(a, b, performance));
  b = a; for (auto& sample : b.samples) sample *= 0.1F;
  CHECK(rendering::crossfadeStylePair(a, b, performance));
  for (auto& sample : b.samples) sample = -sample;
  CHECK(!rendering::crossfadeStylePair(a, b, performance));
  std::fill(b.samples.begin(), b.samples.end(), 0.0F);
  CHECK(rendering::crossfadeStylePair(a, b, performance));
  b.samples.front() = std::numeric_limits<float>::quiet_NaN();
  CHECK(!rendering::crossfadeStylePair(a, b, performance));
  std::stop_source cancelled; cancelled.request_stop();
  CHECK(!rendering::crossfadeStylePair(a, a, performance, cancelled.get_token()));
}

TEST_CASE("spectral paired PCM endpoints and pitch use the same supported composition path") {
  Fixture f;
  for (auto& unit : f.bank.units) unit.renderer = voicebank::RendererHint::SpectralClassic;
  for (const auto amount : {0.0F, 1.0F}) {
    auto paired = f.project;
    paired.findVocalTrack(f.track)->styleSelection.blend->amount = amount;
    auto single = paired;
    single.findVocalTrack(f.track)->styleSelection = {domain::VoiceStyleOrigin::Explicit, amount == 0.0F ? "original" : "soft"};
    const auto actual = f.render(paired), expected = f.render(single);
    CHECK(actual.rendered.audio.startFrame == expected.rendered.audio.startFrame);
    CHECK(actual.rendered.audio.samples == expected.rendered.audio.samples);
  }
  const auto middle = f.render(f.project);
  const auto& pcm = middle.rendered.audio.samples; CHECK(pcm.size() > 18000U);
  const auto pitch = voicebank::analyzePitch(std::span<const float>{pcm.data() + 8000U, 8000U}, 48000U); CHECK(pitch);
  CHECK(std::abs(voicebank::medianVoicedPitch(pitch.value()) - 440.0) < 3.0);
}

TEST_CASE("ordered style pair persists through commands schema migration undo and bank changes") {
  Fixture f;
  auto original = f.project; original.findVocalTrack(f.track)->styleSelection.blend.reset();
  application::EditorSession session{original};
  CHECK(session.execute(std::make_unique<application::EditPerformanceCommand>(
      std::vector<application::NoteExpressionEdit>{}, std::vector<application::RegionDynamicsEdit>{},
      std::vector<application::TrackStyleEdit>{{f.track, f.project.findVocalTrack(f.track)->styleSelection}})));
  const auto paired = session.project();
  const auto encoded = formats::ProjectJsonCodec{}.encode(paired); CHECK(encoded);
  const auto decoded = formats::ProjectJsonCodec{}.decode(encoded.value()); CHECK(decoded); CHECK(decoded.value() == paired);
  CHECK(session.undo()); CHECK(session.project() == original);
  CHECK(session.redo()); CHECK(session.project() == paired);
  auto invalid = paired;
  invalid.findVocalTrack(f.track)->styleSelection.blend->amount = 1.01F; CHECK(!invalid.validate());
  invalid = paired; invalid.findVocalTrack(f.track)->styleSelection.blend->targetStyleId = "original"; CHECK(!invalid.validate());
  auto json = formats::parseJson(encoded.value()); CHECK(json);
  json.value().asObject().at("schemaVersion") = formats::JsonValue{std::int64_t{18}};
  CHECK(!formats::ProjectJsonCodec{}.decode(formats::stringifyJson(json.value())));
  const auto oldEncoded = formats::ProjectJsonCodec{}.encode(original); CHECK(oldEncoded);
  json = formats::parseJson(oldEncoded.value()); CHECK(json);
  json.value().asObject().at("schemaVersion") = formats::JsonValue{std::int64_t{18}};
  const auto migrated = formats::ProjectJsonCodec{}.decode(formats::stringifyJson(json.value())); CHECK(migrated);
  CHECK(migrated.value() == original);
  auto replacement = paired.findVocalTrack(f.track)->voicebank; replacement.version += ".next";
  application::SetTrackVoicebankCommand change{f.track, replacement};
  CHECK(!change.apply(invalid = paired)); CHECK(invalid == paired);
}

TEST_CASE("paired snapshots freeze both styles and bind secondary bytes even at the primary endpoint") {
  Fixture f; f.project.findVocalTrack(f.track)->styleSelection.blend->amount = 0.0F;
  const auto first = f.snapshot(f.project); CHECK(first); CHECK(first.value().sample().blendStyle);
  auto swapped = f.project; auto& selection = swapped.findVocalTrack(f.track)->styleSelection;
  selection.styleId = "soft"; selection.blend->targetStyleId = "original";
  const auto other = f.snapshot(swapped); CHECK(other); CHECK(first.value().contentHash != other.value().contentHash);
  CHECK(voicebank::writeMonoPcm16Wav(f.root / "audio/b.wav", 48000U, test::support::sineWave(48000U, 440.0, 0.5)));
  const auto changed = f.snapshot(f.project); CHECK(changed); CHECK(first.value().contentHash != changed.value().contentHash);
  std::filesystem::rename(f.root / "audio", f.root / "removed-audio");
  CHECK(rendering::PhraseRenderPipeline{}.render(first.value()));
  CHECK(!f.snapshot(f.project));
}

TEST_CASE("paired style admission refuses absent sources explicit style bypass and incompatible timing") {
  Fixture f;
  f.project.findVocalTrack(f.track)->styleSelection.blend->targetStyleId = "absent";
  CHECK(!f.snapshot(f.project));
  f.project.findVocalTrack(f.track)->styleSelection.blend->targetStyleId = "soft";
  const auto segments = rendering::PhraseSegmenter{}.segment(*f.project.findRegion(f.region)); CHECK(segments);
  CHECK(!rendering::RenderSnapshotFactory{}.create(f.project, f.bank, f.track, segments.value().front(), 1U,
      rendering::RenderQuality::Final, f.root, 48000U, "soft"));
  const auto first = f.render(f.project);
  auto incompatible = *first.secondaryTiming;
  ++incompatible.placements.front().desiredVowelOnset;
  CHECK(!rendering::validateStyleBlendTiming(first.timing, incompatible));
  f.bank.units[1].renderer = voicebank::RendererHint::Raw;
  const auto raw = f.snapshot(f.project); CHECK(raw);
  const auto rendered = rendering::PhraseRenderPipeline{}.render(raw.value()); CHECK(!rendered);
  CHECK(rendered.error().message.find("without fallback") != std::string::npos);
}

TEST_CASE("accepted style blend retains default context exact scopes manual islands and arbitrary chunks") {
  using namespace seam;
  using domain::PerformanceChannel;
  using time::Tick;
  Fixture f;
  f.project.findVocalTrack(f.track)->styleSelection.blend->amount = 0.7F;
  auto& region = *f.project.findRegion(f.region);
  const auto pronunciation = phonemizer::resolvePronunciation(region); CHECK(pronunciation);
  const auto& bank = f.project.findVocalTrack(f.track)->voicebank;
  domain::PerformanceTake proposal{.id = "blend", .sourceRegionId = f.region,
      .capturedRevision = region.performance.revision,
      .resource = {domain::SingerResourceKind::Sample, bank.id, bank.version, bank.contentHash},
      .pronunciation = pronunciation.value().identity, .generatorId = "fixture", .generatorVersion = "1",
      .range = {Tick{0}, Tick{1920}},
      .lanes = {{PerformanceChannel::StyleBlend, {{Tick{500}, 0.0}, {Tick{503}, 1.0}}}}};
  application::EditorSession session{f.project};
  const auto initial = f.snapshot(session.project()); CHECK(initial);
  const auto job = session.capturePerformanceJob(); CHECK(job);
  CHECK(session.executePerformanceResult(job.value(), std::make_unique<application::AddPerformanceProposalCommand>(
      f.region, region.performance, proposal)));
  const auto unselected = f.snapshot(session.project()); CHECK(unselected);
  CHECK(unselected.value().contentHash == initial.value().contentHash);
  CHECK(session.execute(std::make_unique<application::SetAcceptedPerformanceCommand>(f.region,
      session.project().findRegion(f.region)->performance,
      std::vector<domain::AcceptedPerformanceSelection>{{"blend", PerformanceChannel::StyleBlend,
          domain::PerformanceTimeRange{Tick{600}, Tick{603}}, Tick{-100}}})));
  const auto& accepted = session.project().findRegion(f.region)->performance;
  CHECK(session.execute(std::make_unique<application::EditPerformanceCommand>(
      std::vector<application::NoteExpressionEdit>{}, std::vector<application::RegionDynamicsEdit>{},
      std::vector<application::TrackStyleEdit>{}, std::vector<application::RegionOwnershipEdit>{{f.region,
          accepted.revision, accepted.ownership, {{PerformanceChannel::StyleBlend,
          domain::PerformanceTimeRange{Tick{601}, Tick{602}}, domain::ManualPerformanceMode::Replace, {}}}}})));
  f.project = session.project();
  const auto frozen = f.snapshot(f.project); CHECK(frozen);
  const auto performance = frozen.value().compiledPerformance; CHECK(performance);
  const auto frame = [&](int tick) { return f.project.tempoMap().sampleFrameAt(Tick{960 + tick}, 48000U); };
  CHECK(performance->at(frame(480) - 1).styleBlend == 0.7F);
  CHECK(performance->at(frame(600) - 1).styleBlend == 0.7F);
  CHECK(performance->at(frame(600)).styleBlend == 0.0F);
  CHECK(performance->at(frame(601)).styleBlend == 0.7F);
  CHECK(performance->at(frame(602) - 1).styleBlend == 0.7F);
  CHECK(performance->at(frame(602)).styleBlend > 0.0F);
  CHECK(performance->at(frame(603)).styleBlend == 0.7F);
  CHECK(performance->at(frame(1440)).styleBlend == 0.7F);
  const auto whole = rendering::PhraseRenderPipeline{}.render(frozen.value()); CHECK(whole);
  const auto& audio = whole.value().rendered.audio;
  // Narrow owned windows straddle every selected/manual boundary. Each child
  // still validates the full context, then crops; never makes local phase decisions.
  const synthesis::PhraseFrameRange window{frame(599), frame(604)};
  for (const auto block : {31U, 67U, 127U}) {
    const auto chunks = rendering::RenderSnapshotFactory{}.splitOwnedOutput(frozen.value(), window, block); CHECK(chunks);
    std::vector<float> joined;
    for (const auto& chunk : chunks.value()) {
      CHECK(chunk.sample().blendStyle);
      const auto rendered = rendering::PhraseRenderPipeline{}.render(chunk); CHECK(rendered);
      joined.insert(joined.end(), rendered.value().rendered.audio.samples.begin(), rendered.value().rendered.audio.samples.end());
    }
    const auto begin = static_cast<std::size_t>(window.start - audio.startFrame);
    CHECK(joined == std::vector<float>(audio.samples.begin() + static_cast<std::ptrdiff_t>(begin),
        audio.samples.begin() + static_cast<std::ptrdiff_t>(begin + joined.size())));
  }
  const auto encoded = formats::ProjectJsonCodec{}.encode(f.project); CHECK(encoded);
  const auto reopened = formats::ProjectJsonCodec{}.decode(encoded.value()); CHECK(reopened);
  CHECK(f.render(reopened.value()).rendered.audio.samples == audio.samples);
  auto noPair = f.project; noPair.findVocalTrack(f.track)->styleSelection.blend.reset();
  const auto refused = f.snapshot(noPair); CHECK(!refused);
  CHECK(refused.error().message.find("StyleBlend") != std::string::npos);
  // A captured job cannot publish after changing the ordered pair/default.
  const auto stale = session.capturePerformanceJob(); CHECK(stale);
  auto updated = session.project().findVocalTrack(f.track)->styleSelection;
  updated.blend->amount = 0.3F;
  CHECK(session.execute(std::make_unique<application::EditPerformanceCommand>(
      std::vector<application::NoteExpressionEdit>{}, std::vector<application::RegionDynamicsEdit>{},
      std::vector<application::TrackStyleEdit>{{f.track, updated}})));
  CHECK(!session.validatePerformanceJob(stale.value()));
}

TEST_CASE("paired styles bind each multi-nucleus alignment and share edited target landmarks") {
  Fixture f;
  auto& region = *f.project.findRegion(f.region);
  region.notes.front().phoneticHint = "a a";
  region.lyrics.front().surface = U"ああ";
  region.phonemeOverrides = {{.key = {f.note, 1U}, .timing = {.startOffset = 300000}, .locked = true}};
  for (auto& unit : f.bank.units) unit.phones = {"a", "a"};
  std::filesystem::create_directories(f.root / "alignments");
  const auto writeAlignment = [&](std::size_t index, time::SampleFrame secondFrame) {
    const auto& unit = f.bank.units[index];
    const auto bytes = core::readFileBytesLimited(f.root / unit.audioPath, 1U << 20U); CHECK(bytes);
    const auto hash = core::sha256Hex(bytes.value());
    const synthesis::SourcePhonemeAlignment alignment{unit.id, hash, {{"a", 3600}, {"a", secondFrame}}};
    const auto json = synthesis::encodeSourcePhonemeAlignment(alignment, unit, hash, 24000); CHECK(json);
    CHECK(core::durableAtomicWriteText(f.root / "alignments" / (core::sha256Hex(unit.id) + ".json"), json.value()));
  };
  writeAlignment(0U, 15000);
  CHECK(!f.snapshot(f.project)); // Both sides need their own hash-bound landmarks.
  writeAlignment(1U, 15000);
  const auto initial = f.snapshot(f.project); CHECK(initial);
  const auto rendered = rendering::PhraseRenderPipeline{}.render(initial.value()); CHECK(rendered);
  CHECK(rendered.value().timing.placements.front().phonemeTargets.size() == 2U);
  CHECK(rendering::validateStyleBlendTiming(rendered.value().timing, *rendered.value().secondaryTiming));
  writeAlignment(1U, 15120); // Source landmarks may differ, target landmarks may not.
  const auto changed = f.snapshot(f.project); CHECK(changed);
  CHECK(changed.value().contentHash != initial.value().contentHash);
  std::filesystem::rename(f.root / "alignments", f.root / "removed-alignments");
  CHECK(rendering::PhraseRenderPipeline{}.render(initial.value()));
  CHECK(!f.snapshot(f.project));
}

TEST_CASE("paired region cache publishes both source count and pair renderer provenance") {
  Fixture f;
  rendering::PcmCache cache{f.root / "pcm-cache"};
  const auto render = [&] { return rendering::ProductionRegionRenderer{}.render(f.project, f.bank, f.root,
      f.track, f.region, 1U, 48000U, rendering::RenderQuality::Final, {}, {}, &cache); };
  const auto first = render(); CHECK(first); CHECK(first.value().cacheHits == 0U);
  CHECK(first.value().unitCount == 2U);
  CHECK(first.value().phrases.front().rendererIdentity == "seam.pcm-style-crossfade.v1");
  const auto cached = render(); CHECK(cached); CHECK(cached.value().cacheHits == 1U);
  CHECK(cached.value().mono == first.value().mono);
  CHECK(cached.value().phrases.front().rendererIdentity == first.value().phrases.front().rendererIdentity);
  f.project.findVocalTrack(f.track)->styleSelection.blend->amount = 0.25F;
  const auto edited = render(); CHECK(edited); CHECK(edited.value().cacheHits == 0U);
  CHECK(edited.value().mono != first.value().mono);
  // The sample renderer itself never advertises pair composition as a scalar.
  CHECK(!synthesis::rendererCapabilities(synthesis::RendererCarrier::SampleBank).supports(synthesis::RendererControl::StyleBlend));
}

TEST_CASE("multi-placement style endpoints preserve standalone project PCM and final WAV seam samples") {
  Fixture f;
  auto* region = f.project.findRegion(f.region);
  region->notes.front().durationTick = time::Tick{480};
  for (std::int64_t i = 1; i < 3; ++i) {
    auto [lyric, note] = f.factory.makeNote(time::Tick{480 + i * 480}, time::Tick{480}, 69U,
        U"あ", domain::Language::Japanese);
    note.phoneticHint = "a"; region->lyrics.push_back(lyric); region->notes.push_back(note);
  }
  // Distinct attack/release envelopes, same source landmarks and target timing.
  // Preserve phase so this checks seams rather than the cancellation refusal.
  auto secondary = test::support::sineWave(48000U, 440.0, 0.5);
  for (std::size_t i = 0; i < secondary.size(); ++i) {
    const auto attack = std::min(1.0, static_cast<double>(i) / 2000.0);
    const auto release = std::min(1.0, static_cast<double>(secondary.size() - i) / 3000.0);
    secondary[i] *= static_cast<float>(0.8 * attack * release);
  }
  CHECK(voicebank::writeMonoPcm16Wav(f.root / "audio/b.wav", 48000U, secondary));
  const std::vector<rendering::TrackVoicebankSource> sources{{.trackId = f.track, .manifest = f.bank,
      .bankRoot = f.root, .contentHash = std::string(64U, 'a')}};
  rendering::PcmCache cache{f.root / "project-pair-cache"};
  const auto projectRender = [&](const domain::Project& score) {
    return rendering::ProductionProjectRenderer{}.render(score, sources, f.track, f.region, 1U,
        48000U, rendering::RenderQuality::Final, {}, &cache);
  };
  for (const float amount : {0.0F, 1.0F}) {
    f.project.findVocalTrack(f.track)->styleSelection.blend->amount = amount;
    const auto paired = projectRender(f.project); CHECK(paired);
    CHECK(paired.value().activeUnitPlan.size() == 3U);
    const auto cached = projectRender(f.project); CHECK(cached);
    CHECK(cached.value().cacheHits > 0U); CHECK(cached.value().activeUnitPlan == paired.value().activeUnitPlan);
    auto standalone = f.project;
    standalone.findVocalTrack(f.track)->styleSelection = {domain::VoiceStyleOrigin::Explicit,
        amount == 0.0F ? "original" : "soft"};
    const auto expected = projectRender(standalone); CHECK(expected);
    CHECK(paired.value().interleaved == expected.value().interleaved);
    CHECK(voicebank::writeWav(f.root / "paired-export.wav",
        {.sampleRate = 48000U, .channels = paired.value().channelCount, .sampleFormat = voicebank::WavSampleFormat::Float32},
        std::span<const float>{paired.value().interleaved.data(), paired.value().interleaved.size()}));
    const auto exported = voicebank::readWav(f.root / "paired-export.wav"); CHECK(exported);
    CHECK(std::equal(exported.value().interleaved.begin(), exported.value().interleaved.end(), expected.value().interleaved.begin(), expected.value().interleaved.end()));
  }
  f.project.findVocalTrack(f.track)->styleSelection.blend->amount = 0.5F;
  const auto frozen = f.snapshot(f.project); CHECK(frozen);
  const auto whole = rendering::PhraseRenderPipeline{}.render(frozen.value()); CHECK(whole);
  CHECK(whole.value().timing.placements.size() == 3U);
  const auto& audio = whole.value().rendered.audio;
  const auto chunks = rendering::RenderSnapshotFactory{}.splitOwnedOutput(frozen.value(),
      {audio.startFrame, audio.startFrame + static_cast<time::SampleFrame>(audio.samples.size())}, 4096U);
  CHECK(chunks); std::vector<float> stitched;
  for (const auto& chunk : chunks.value()) {
    const auto rendered = rendering::PhraseRenderPipeline{}.render(chunk); CHECK(rendered);
    stitched.insert(stitched.end(), rendered.value().rendered.audio.samples.begin(), rendered.value().rendered.audio.samples.end());
  }
  CHECK(stitched == audio.samples);
}

TEST_CASE("paired mixing rejects aggregate work and disjoint spans before publishing audio") {
  Fixture f;
  auto performance = f.performance(0.5F);
  synthesis::PhraseAudio a{.startFrame = 0, .samples = {0.1F, 0.2F}};
  auto b = a; b.startFrame = static_cast<time::SampleFrame>(rendering::kMaximumStyleBlendFrames);
  CHECK(!rendering::crossfadeStylePair(a, b, performance));
  // One existing source allocation already consumes the pair's complete budget;
  // adding even a two-frame arm rejects before allocating an output buffer.
  b.startFrame = 0; b.samples.resize(rendering::kMaximumStyleBlendFrames);
  CHECK(!rendering::crossfadeStylePair(a, b, performance));
  b.samples.clear(); CHECK(!rendering::crossfadeStylePair(a, b, performance));
  f.project.findRegion(f.region)->notes.front().durationTick = time::Tick{700000};
  f.project.findRegion(f.region)->durationTick = time::Tick{701000};
  // Use a direct full-note segment so the generic short-phrase segmentation
  // policy cannot disguise the deliberately excessive two-arm work request.
  const rendering::PhraseSegment segment{.id = "long-pair", .regionId = f.region,
      .startTick = time::Tick{480}, .endTick = time::Tick{700480}, .noteIds = {f.note}};
  const auto frozen = rendering::RenderSnapshotFactory{}.create(f.project, f.bank, f.track, segment,
      1U, rendering::RenderQuality::Final, f.root, 48000U); CHECK(frozen);
  const auto refused = rendering::PhraseRenderPipeline{}.render(frozen.value()); CHECK(!refused);
  CHECK(refused.error().message.find("aggregate requested render work") != std::string::npos);
}
