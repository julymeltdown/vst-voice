#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/core/sha256.hpp"
#include "seam/core/file_io.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/rendering/region_renderer.hpp"
#include "seam/synthesis/unit_selection.hpp"
#include "seam/ui/unit_lane_model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>

namespace {
using namespace seam;
struct Fixture {
  application::ProjectFactory factory{771931U};
  domain::Project project{factory.createProject("Contextual selection")};
  domain::TrackId track{factory.addVocalTrack(project, "Singer")};
  domain::RegionId region{factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{1920})};
  voicebank::Manifest bank{test::support::makeManifest({})};
  std::vector<domain::PhonemeToken> tokens;
  std::map<std::string, voicebank::AudioBuffer> sources;
  std::filesystem::path root{test::support::temporaryDirectory("contextual-selection")};
  Fixture() {
    std::filesystem::create_directories(root / "audio");
    const std::vector<std::string> phones{"a", "i", "u"};
    for (std::size_t i = 0; i < phones.size(); ++i) {
      auto [lyric, note] = factory.makeNote(time::Tick{static_cast<std::int64_t>(i) * 480},
          time::Tick{480}, 69U, U"あ", domain::Language::Japanese);
      note.phoneticHint = phones[i];
      domain::PhonemeToken token; token.key = {note.id, 0}; token.symbol = phones[i];
      tokens.push_back(token);
      project.findRegion(region)->lyrics.push_back(lyric);
      project.findRegion(region)->notes.push_back(note);
    }
    project.findVocalTrack(track)->voicebank = {bank.id, bank.version, std::string(64U, 'a')};
    project.findVocalTrack(track)->styleSelection = {domain::VoiceStyleOrigin::Explicit, "original"};
  }
  void add(std::string id, std::vector<std::string> phones, float head, float tail, int take = 1) {
    auto unit = test::support::makeUnit(id, std::move(phones), "audio/" + id + ".wav", 69,
        voicebank::UnitKind::Sustain, 3840);
    unit.take = take;
    voicebank::AudioBuffer audio{.sampleRate = 48000U, .channels = 1U, .interleaved = std::vector<float>(3840U, head)};
    std::fill(audio.interleaved.begin() + 1920, audio.interleaved.end(), tail);
    CHECK(voicebank::writeMonoPcm16Wav(root / unit.audioPath, audio.sampleRate, audio.interleaved));
    sources.emplace(id, audio); bank.units.push_back(unit);
  }
  void addHarmonic(std::string id, std::vector<std::string> phones,
                   bool headBright, bool tailBright) {
    auto unit = test::support::makeUnit(id, std::move(phones), "audio/" + id + ".wav", 69,
        voicebank::UnitKind::Sustain, 3840);
    voicebank::AudioBuffer audio{.sampleRate = 48000U, .channels = 1U,
        .interleaved = std::vector<float>(3840U)};
    constexpr std::size_t halfFrames = 1920U;
    const auto writeHalf = [&](std::size_t offset, bool bright) {
      const auto harmonicGain = bright ? 0.75 : 0.0;
      std::vector<double> wave(halfFrames);
      double square = 0.0;
      for (std::size_t frame = 0U; frame < halfFrames; ++frame) {
        const auto phase = 2.0 * std::numbers::pi * 400.0 * static_cast<double>(frame) / 48000.0;
        wave[frame] = std::sin(phase) + harmonicGain * std::sin(2.0 * phase);
        square += wave[frame] * wave[frame];
      }
      const auto scale = 0.25 / std::sqrt(square / static_cast<double>(halfFrames));
      for (std::size_t frame = 0U; frame < halfFrames; ++frame) {
        audio.interleaved[offset + frame] = static_cast<float>(wave[frame] * scale);
      }
    };
    writeHalf(0U, headBright);
    writeHalf(halfFrames, tailBright);
    CHECK(voicebank::writeMonoPcm16Wav(root / unit.audioPath, audio.sampleRate, audio.interleaved));
    sources.emplace(id, std::move(audio));
    bank.units.push_back(std::move(unit));
  }
  std::vector<synthesis::UnitJoinAnalysis> analysis(synthesis::UnitSelectionBudget& budget) const {
    std::vector<synthesis::UnitJoinAnalysis> values;
    for (const auto& unit : bank.units) {
      auto measured = synthesis::analyzeUnitJoin(unit, sources.at(unit.id), std::string(64U, 'a'), budget);
      CHECK(measured); values.push_back(std::move(measured).value());
    }
    return values;
  }
  core::Result<synthesis::UnitPlan> select(synthesis::UnitSelectionContext context = {}) const {
    return synthesis::DeterministicUnitSelector{}.select(bank, *project.findRegion(region), tokens,
        "original", {}, {}, false, context);
  }
  core::Result<rendering::RenderSnapshot> snapshot() const {
    const auto segments = rendering::PhraseSegmenter{}.segment(*project.findRegion(region)); CHECK(segments);
    CHECK(segments.value().size() == 1U);
    return rendering::RenderSnapshotFactory{}.create(project, bank, track, segments.value().front(), 1U,
        rendering::RenderQuality::Final, root, 48000U);
  }
  void chain() {
    add("a-cheap", {"a"}, 0.2F, 0.05F);
    add("a-context", {"a"}, 0.2F, 0.5F, 2);
    add("i", {"i"}, 0.5F, 0.5F);
    add("u", {"u"}, 0.5F, 0.5F);
  }
};
}  // namespace

TEST_CASE("contextual selection retains a locally worse predecessor for the best complete sequence") {
  Fixture f; f.chain();
  const auto metadata = f.select(); CHECK(metadata);
  CHECK(metadata.value().entries.front().unitId == "a-cheap");
  synthesis::UnitSelectionBudget budget;
  const auto analysis = f.analysis(budget);
  const auto selected = f.select({.analysis = analysis, .requireAcoustic = true, .budget = &budget}); CHECK(selected);
  CHECK(selected.value().entries.size() == 3U);
  CHECK(selected.value().entries.front().unitId == "a-context");
  CHECK_NEAR(selected.value().totalScore, 0.001, 1e-8);
  CHECK(selected.value().entries[1].rationale.predecessor == "a-context");
  CHECK(selected.value().entries[1].rationale.joined);
  CHECK_NEAR(selected.value().entries[1].rationale.incomingCost, 0.0, 1e-8);
  CHECK(synthesis::describeUnitSelection(selected.value().entries[1]).find("Source-boundary proxy") != std::string::npos);
  std::reverse(f.bank.units.begin(), f.bank.units.end());
  const auto permuted = f.select({.analysis = analysis, .requireAcoustic = true}); CHECK(permuted);
  CHECK(permuted.value() == selected.value());
}

TEST_CASE("contextual selection optimizes outgoing joins rather than the cheapest immediate edge") {
  Fixture f;
  f.add("a", {"a"}, 0.1F, 0.1F);
  f.add("i-greedy", {"i"}, 0.1F, 0.01F);
  f.add("i-global", {"i"}, 0.2F, 0.5F, 2);
  f.add("u", {"u"}, 0.5F, 0.5F);
  synthesis::UnitSelectionBudget budget; const auto analysis = f.analysis(budget);
  const auto selected = f.select({.analysis = analysis, .requireAcoustic = true}); CHECK(selected);
  CHECK(selected.value().entries[1].unitId == "i-global");
  CHECK(selected.value().entries[1].rationale.incomingCost > 3.0);
  const auto& rationale = selected.value().entries[1].rationale;
  CHECK_NEAR(rationale.incomingCost,
      rationale.levelCost + rationale.correlationCost + rationale.spectralEnvelopeCost, 1e-12);
  CHECK(rationale.levelCost > 3.0);
  CHECK_NEAR(rationale.correlationCost, 0.0, 1e-12);
  CHECK_NEAR(rationale.spectralEnvelopeCost, 0.0, 1e-12);
  const auto explanation = synthesis::describeUnitSelection(selected.value().entries[1]);
  CHECK(explanation.find("level ") != std::string::npos);
  CHECK(explanation.find("short-lag correlation ") != std::string::npos);
  CHECK(explanation.find("spectral envelope ") != std::string::npos);
  CHECK(explanation.find("score points (lower is better)") != std::string::npos);
  CHECK(explanation.find("correlation=sum of 4 abs(delta rho)") != std::string::npos);
  CHECK(explanation.find("incoming=level+correlation+spectral") != std::string::npos);
  CHECK(explanation.find("level=0.5*min(24 dB,abs(delta level dB))") != std::string::npos);
  CHECK(explanation.find("spectral=0.25*min(24 dB,mean of 8 abs(delta band dB))") != std::string::npos);
  CHECK(selected.value().totalScore < 3.02);
  CHECK_NEAR(selected.value().entries.back().rationale.cumulativeCost, selected.value().totalScore, 1e-12);
}

TEST_CASE("selection rationale explains local score components and their formulas") {
  Fixture f;
  f.add("full", {"a", "i", "u"}, 0.5F, 0.5F);
  auto& unit = f.bank.units.back();
  unit.rootMidi = 72;
  unit.priority = 4;
  unit.take = 3;

  const auto selected = f.select(); CHECK(selected);
  CHECK(selected.value().entries.size() == 1U);
  const auto& entry = selected.value().entries.front();
  CHECK_NEAR(entry.rationale.localScore.pitchPenalty, 30.0, 1e-12);
  CHECK_NEAR(entry.rationale.localScore.multiPhoneBonus, -4.0, 1e-12);
  CHECK_NEAR(entry.rationale.localScore.priorityBonus, -1.0, 1e-12);
  CHECK_NEAR(entry.rationale.localScore.takePenalty, 0.002, 1e-12);
  CHECK_NEAR(entry.rationale.localScore.total(), entry.score, 1e-12);
  CHECK_NEAR(entry.score, 25.002, 1e-12);

  const auto explanation = synthesis::describeUnitSelection(entry);
  CHECK(explanation.find("pitch 30.000") != std::string::npos);
  CHECK(explanation.find("multi-phone -4.000") != std::string::npos);
  CHECK(explanation.find("priority -1.000") != std::string::npos);
  CHECK(explanation.find("take 0.002") != std::string::npos);
  CHECK(explanation.find("pitch=10*abs(root MIDI-target MIDI)") != std::string::npos);
  CHECK(explanation.find("multi-phone=-2*extra phones") != std::string::npos);
  CHECK(explanation.find("priority=-0.25*priority") != std::string::npos);
  CHECK(explanation.find("take=0.001*max(0,take-1)") != std::string::npos);
  CHECK(explanation.find("route=previous route+local+incoming") != std::string::npos);
}

TEST_CASE("contextual selection scores gain-normalized spectral envelopes deterministically") {
  Fixture f;
  // All candidates have equal RMS, pitch, priority, take, and phone coverage.
  // Only the selected first unit's tail timbre differs at the a -> i join.
  f.addHarmonic("a-a-dark-tail", {"a"}, false, false);
  f.addHarmonic("a-z-bright-tail", {"a"}, false, true);
  f.addHarmonic("i-bright-head", {"i"}, true, false);
  f.addHarmonic("u-dark", {"u"}, false, false);
  const auto metadata = f.select(); CHECK(metadata);
  CHECK(metadata.value().entries.front().unitId == "a-a-dark-tail");

  synthesis::UnitSelectionBudget budget;
  const auto evidence = f.analysis(budget);
  const auto find = [&](std::string_view id) -> const synthesis::UnitJoinAnalysis& {
    const auto item = std::find_if(evidence.begin(), evidence.end(),
        [&](const auto& value) { return value.unitId == id; });
    CHECK(item != evidence.end());
    return *item;
  };
  const auto spectralDistance = [&](const synthesis::SourceBoundaryFeatures& first,
                                    const synthesis::SourceBoundaryFeatures& second) {
    double distance = 0.0;
    for (std::size_t band = 0U; band < first.spectralEnvelopeDb.size(); ++band) {
      distance += std::abs(first.spectralEnvelopeDb[band] - second.spectralEnvelopeDb[band]);
    }
    return distance;
  };
  const auto& darkTail = find("a-a-dark-tail").tail;
  const auto& brightTail = find("a-z-bright-tail").tail;
  const auto& brightHead = find("i-bright-head").head;
  CHECK_NEAR(darkTail.levelDb, brightTail.levelDb, 1e-4);
  CHECK(spectralDistance(brightTail, brightHead) < spectralDistance(darkTail, brightHead));

  const auto context = synthesis::UnitSelectionContext{
      .analysis = evidence, .requireAcoustic = true, .budget = &budget};
  const auto selected = f.select(context); CHECK(selected);
  CHECK(selected.value().entries.front().unitId == "a-z-bright-tail");
  const auto expectedSpectralCost = 0.25 * std::min(24.0,
      spectralDistance(brightTail, brightHead) / 8.0);
  CHECK_NEAR(selected.value().entries[1].rationale.spectralEnvelopeCost,
      expectedSpectralCost, 1e-12);
  CHECK(synthesis::describeUnitSelection(selected.value().entries[1]).find("spectral envelope") != std::string::npos);

  std::reverse(f.bank.units.begin(), f.bank.units.end());
  const auto reversed = f.select({.analysis = evidence, .requireAcoustic = true}); CHECK(reversed);
  CHECK(reversed.value() == selected.value());
}

TEST_CASE("repeated occurrences and exact ties remain deterministic across inventory order") {
  Fixture f;
  for (auto& token : f.tokens) token.symbol = "a";
  f.add("a-z", {"a"}, 0.5F, 0.5F); f.add("a-a", {"a"}, 0.5F, 0.5F);
  synthesis::UnitSelectionBudget budget; const auto analysis = f.analysis(budget);
  const auto selected = f.select({.analysis = analysis, .requireAcoustic = true}); CHECK(selected);
  CHECK(selected.value().entries.size() == 3U);
  for (std::size_t i = 0; i < 3U; ++i) {
    CHECK(selected.value().entries[i].unitId == "a-a");
    CHECK(selected.value().entries[i].tokenStart == i);
  }
  std::reverse(f.bank.units.begin(), f.bank.units.end());
  const auto reversed = f.select({.analysis = analysis, .requireAcoustic = true}); CHECK(reversed);
  CHECK(reversed.value() == selected.value());
}

TEST_CASE("forced intervals cannot be jumped over and overlapping constraints fail explicitly") {
  Fixture f;
  f.add("long", {"a", "i", "u"}, 0.5F, 0.5F);
  f.add("forced-i", {"i"}, 0.5F, 0.5F);
  auto& overrides = f.project.findRegion(f.region)->unitSelectionOverrides;
  overrides.push_back({.startKey = f.tokens[1].key, .tokenCount = 1U, .unitId = "forced-i",
      .renderer = domain::UnitRendererKind::SpectralClassic, .locked = true});
  CHECK(!f.select());  // Legacy search could take long and erase the forced start.
  f.add("a", {"a"}, 0.5F, 0.5F); f.add("u", {"u"}, 0.5F, 0.5F);
  auto selected = f.select(); CHECK(selected);
  CHECK(selected.value().entries[1].unitId == "forced-i"); CHECK(selected.value().entries[1].forced);
  CHECK(selected.value().entries[1].renderer == domain::UnitRendererKind::SpectralClassic);
  overrides.push_back({.startKey = f.tokens[2].key, .tokenCount = 1U, .unitId = "u", .locked = true});
  CHECK(f.select());  // Adjacent is legal.
  overrides.push_back(overrides.front());
  auto overlap = f.select(); CHECK(!overlap); CHECK(overlap.error().code == core::ErrorCode::Conflict);
  CHECK(overlap.error().message.find("overlap") != std::string::npos);
}

TEST_CASE("selection budgets accept exact work and reject extra work without pruning") {
  Fixture f; f.chain();
  synthesis::UnitSelectionBudget measured;
  CHECK(f.select({.budget = &measured}));
  synthesis::UnitSelectionBudget exact; exact.limits = measured.used;
  CHECK(f.select({.budget = &exact})); CHECK(exact.used == measured.used);
  synthesis::UnitSelectionBudget shortBudget; shortBudget.limits = measured.used;
  --shortBudget.limits[static_cast<std::size_t>(synthesis::SelectionWork::Edges)];
  auto shortResult = f.select({.budget = &shortBudget}); CHECK(!shortResult);
  CHECK(shortResult.error().code == core::ErrorCode::Unsupported);
  std::stop_source cancelled; cancelled.request_stop();
  CHECK(!f.select({.stop = cancelled.get_token()}));
  const auto seed = f.bank.units.front(); f.bank.units.clear();
  f.tokens.resize(1U);
  for (std::size_t i = 0; i < synthesis::kMaximumSelectionStatesAtBoundary; ++i) {
    auto unit = seed; unit.id = "candidate-" + std::to_string(i); f.bank.units.push_back(unit);
  }
  CHECK(f.select());
  auto extra = seed; extra.id = "one-too-many"; f.bank.units.push_back(extra);
  auto overflow = f.select(); CHECK(!overflow);
  CHECK(overflow.error().message.find("per-boundary") != std::string::npos);
}

TEST_CASE("join proxy uses playable crop gain and physical window with finite silent short stereo behavior") {
  Fixture f; f.add("a", {"a"}, 0.2F, 0.4F);
  auto unit = f.bank.units.front(); auto audio = f.sources.at("a");
  synthesis::UnitSelectionBudget budget;
  const auto original = synthesis::analyzeUnitJoin(unit, audio, std::string(64U, 'a'), budget); CHECK(original);
  unit.gainDb = 6.0F;
  const auto gain = synthesis::analyzeUnitJoin(unit, audio, std::string(64U, 'a'), budget); CHECK(gain);
  CHECK_NEAR(gain.value().head.levelDb - original.value().head.levelDb, 6.0, 1e-10);
  CHECK(gain.value().head.spectralEnvelopeDb == original.value().head.spectralEnvelopeDb);
  CHECK(gain.value().tail.spectralEnvelopeDb == original.value().tail.spectralEnvelopeDb);
  audio.channels = 2U; audio.interleaved.assign(7680U, 0.2F);
  for (std::size_t i = 1; i < audio.interleaved.size(); i += 2U) audio.interleaved[i] = -0.2F;
  auto silent = synthesis::analyzeUnitJoin(unit, audio, std::string(64U, 'a'), budget); CHECK(silent);
  for (const auto value : silent.value().head.correlation) CHECK(value == 0.0);
  CHECK(std::isfinite(silent.value().head.levelDb));
  unit.markers = {}; unit.markers.audioEnd = 1;
  audio.channels = 1; audio.interleaved = {0.4F};
  CHECK(synthesis::analyzeUnitJoin(unit, audio, std::string(64U, 'a'), budget));
  audio.interleaved[0] = std::numeric_limits<float>::quiet_NaN();
  CHECK(!synthesis::analyzeUnitJoin(unit, audio, std::string(64U, 'a'), budget));
  audio.interleaved = {0.4F};
  CHECK(!synthesis::analyzeUnitJoin(unit, audio, "unchecked", budget));
  unit.markers.audioEnd = 2;
  CHECK(!synthesis::analyzeUnitJoin(unit, audio, std::string(64U, 'a'), budget));
}

TEST_CASE("spectral-envelope work is charged before bounded analysis completes") {
  Fixture f; f.addHarmonic("spectral", {"a"}, true, false);
  const auto& unit = f.bank.units.front();
  const auto& audio = f.sources.at(unit.id);
  synthesis::UnitSelectionBudget measured;
  CHECK(synthesis::analyzeUnitJoin(unit, audio, std::string(64U, 'a'), measured));
  const auto spectralIndex = static_cast<std::size_t>(synthesis::SelectionWork::SpectralOps);
  CHECK(measured.used[spectralIndex] > 0U);
  auto shortBudget = synthesis::UnitSelectionBudget{};
  shortBudget.limits[spectralIndex] = measured.used[spectralIndex] - 1U;
  const auto rejected = synthesis::analyzeUnitJoin(unit, audio, std::string(64U, 'a'), shortBudget);
  CHECK(!rejected);
  CHECK(rejected.error().code == core::ErrorCode::Unsupported);
}

TEST_CASE("source join costs reset at score rests and absent evidence never becomes zero cost") {
  Fixture f; f.chain();
  synthesis::UnitSelectionBudget budget; auto analysis = f.analysis(budget);
  f.project.findRegion(f.region)->notes[1].startTick = time::Tick{600};
  const auto rest = f.select({.analysis = analysis, .requireAcoustic = true}); CHECK(rest);
  CHECK(rest.value().entries.front().unitId == "a-cheap");
  CHECK(!rest.value().entries[1].rationale.joined);
  analysis.pop_back();
  CHECK(!f.select({.analysis = analysis, .requireAcoustic = true}));
}

TEST_CASE("normal snapshot hashes losing competitors and preserves frozen decisions in owned output") {
  Fixture f; f.chain();
  const auto snapshot = f.snapshot(); CHECK(snapshot);
  const auto& plan = *snapshot.value().sample().unitPlan;
  CHECK(plan.entries.front().unitId == "a-context"); CHECK(plan.selectionContextHash.size() == 64U);
  CHECK(plan.entries[1].rationale.evidenceHash == plan.selectionContextHash);
  std::reverse(f.bank.units.begin(), f.bank.units.end());
  const auto permuted = f.snapshot(); CHECK(permuted);
  CHECK(permuted.value().contentHash == snapshot.value().contentHash);
  auto rendered = rendering::PhraseRenderPipeline{}.render(snapshot.value()); CHECK(rendered);
  auto loser = f.sources.at("a-cheap").interleaved;
  std::fill(loser.begin() + 1920, loser.end(), 0.02F);
  CHECK(voicebank::writeMonoPcm16Wav(f.root / "audio/a-cheap.wav", 48000U, loser));
  const auto changed = f.snapshot(); CHECK(changed);
  CHECK(changed.value().contentHash != snapshot.value().contentHash);
  CHECK(changed.value().sample().unitPlan->entries.front().unitId == "a-context");
  CHECK(std::filesystem::remove(f.root / "audio/a-cheap.wav"));
  CHECK(!f.snapshot());  // Do not silently discard an unreadable competitor.
  const auto& whole = rendered.value().rendered.audio;
  const auto chunks = rendering::RenderSnapshotFactory{}.splitOwnedOutput(snapshot.value(),
      {whole.startFrame, whole.startFrame + static_cast<time::SampleFrame>(whole.samples.size())}, 4096U);
  CHECK(chunks);
  std::vector<float> stitched;
  for (const auto& chunk : chunks.value()) {
    CHECK(*chunk.sample().unitPlan == plan);
    const auto part = rendering::PhraseRenderPipeline{}.render(chunk); CHECK(part);
    stitched.insert(stitched.end(), part.value().rendered.audio.samples.begin(), part.value().rendered.audio.samples.end());
  }
  CHECK(stitched == whole.samples);
}

TEST_CASE("cold and cached region results carry the actual contextual decision and rationale") {
  Fixture f; f.chain();
  rendering::PcmCache cache{f.root / "pcm-cache"};
  const auto run = [&] { return rendering::ProductionRegionRenderer{}.render(f.project, f.bank, f.root,
      f.track, f.region, 1U, 48000U, rendering::RenderQuality::Final, {}, {}, &cache); };
  const auto cold = run(); CHECK(cold);
  const auto warm = run(); CHECK(warm); CHECK(warm.value().cacheHits > 0U);
  CHECK(cold.value().mono == warm.value().mono); CHECK(cold.value().unitPlan == warm.value().unitPlan);
  CHECK(cold.value().unitPlan.front().unitId == "a-context");
  CHECK(cold.value().unitPlan.front().rationale.acoustic);
  const auto snapshot = f.snapshot(); CHECK(snapshot);
  const auto rendered = rendering::PhraseRenderPipeline{}.render(snapshot.value()); CHECK(rendered);
  ui::UnitLaneModel lane;
  lane.rebuild(*snapshot.value().project, snapshot.value().project->vocalTracks().front().regions.front(),
      *snapshot.value().phonemes, *snapshot.value().sample().unitPlan, rendered.value().timing,
      &rendered.value().rendered, ui::TimelineTransform{f.project.ppq(), 120.0, time::Tick{0}},
      72.0, 200.0, 32.0, 48000U);
  CHECK(lane.visuals().size() == 3U);
  CHECK(lane.visuals()[1].selectionRationale == synthesis::describeUnitSelection(cold.value().unitPlan[1]));
}

TEST_CASE("new alignment eligibility changes decision identity even when the selected sequence does not change") {
  Fixture f; f.chain(); f.add("long-loser", {"a", "i", "u"}, 0.2F, 0.5F);
  f.bank.units.back().priority = -100;
  const auto before = f.snapshot(); CHECK(before);
  const auto& unit = f.bank.units.back();
  const auto bytes = core::readFileBytesLimited(f.root / unit.audioPath, 65536U); CHECK(bytes);
  const auto hash = core::sha256Hex(bytes.value());
  const synthesis::SourcePhonemeAlignment alignment{unit.id, hash, {{"a", 960}, {"i", 1920}, {"u", 2880}}};
  const auto json = synthesis::encodeSourcePhonemeAlignment(alignment, unit, hash, 3840); CHECK(json);
  std::filesystem::create_directories(f.root / "alignments");
  CHECK(core::durableAtomicWriteText(f.root / "alignments" / (core::sha256Hex(unit.id) + ".json"), json.value()));
  const auto after = f.snapshot(); CHECK(after);
  CHECK(after.value().contentHash != before.value().contentHash);
  CHECK(after.value().sample().unitPlan->entries.size() == 3U);
  CHECK(after.value().sample().unitPlan->entries.front().unitId == before.value().sample().unitPlan->entries.front().unitId);
  const auto oldAudio = rendering::PhraseRenderPipeline{}.render(before.value()); CHECK(oldAudio);
  const auto newAudio = rendering::PhraseRenderPipeline{}.render(after.value()); CHECK(newAudio);
  CHECK(oldAudio.value().rendered.audio.samples == newAudio.value().rendered.audio.samples);
}

TEST_CASE("selection work budget is shared across repeated arms and source crop ignores outside padding") {
  Fixture f; f.chain();
  synthesis::UnitSelectionBudget measured; CHECK(f.select({.budget = &measured}));
  synthesis::UnitSelectionBudget pair;
  for (std::size_t i = 0; i < pair.limits.size(); ++i) pair.limits[i] = measured.used[i] * 2U;
  CHECK(f.select({.budget = &pair})); CHECK(f.select({.budget = &pair}));
  CHECK(!f.select({.budget = &pair}));
  auto unit = f.bank.units.front(); auto audio = f.sources.at(unit.id);
  unit.markers.audioOffset = 200; unit.markers.audioEnd = 3600;
  synthesis::UnitSelectionBudget samples;
  const auto before = synthesis::analyzeUnitJoin(unit, audio, std::string(64U, 'a'), samples); CHECK(before);
  std::fill(audio.interleaved.begin(), audio.interleaved.begin() + 200, 0.99F);
  std::fill(audio.interleaved.begin() + 3600, audio.interleaved.end(), 0.99F);
  const auto after = synthesis::analyzeUnitJoin(unit, audio, std::string(64U, 'b'), samples); CHECK(after);
  CHECK(before.value().head == after.value().head); CHECK(before.value().tail == after.value().tail);
  std::stop_source stop; stop.request_stop();
  CHECK(!synthesis::analyzeUnitJoin(unit, audio, std::string(64U, 'a'), samples, stop.get_token()));
}

namespace {
bool mentions(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
}  // namespace

// The words a failed selection gives the creator: which sound has no unit, whose lyric it is and where
// its note begins. The Fixture's three notes sing a, i and u at ticks 0, 480 and 960, each with the
// lyric あ, and the project counts 960 ticks to a beat in 4/4.

TEST_CASE("a phrase no chain of units can cover names the sound, its lyric and where the note begins") {
  Fixture f;
  f.add("a", {"a"}, 0.5F, 0.5F);
  f.add("i", {"i"}, 0.5F, 0.5F);
  const auto selected = f.select({.meters = &f.project.meterMap()});
  CHECK(!selected);
  if (selected) return;
  CHECK(selected.error().code == core::ErrorCode::NotFound);
  const auto& message = selected.error().message;
  CHECK(mentions(message, "sound \"u\""));
  CHECK(mentions(message, "lyric \"あ\""));
  CHECK(mentions(message, "bar 1, beat 2"));
  CHECK(selected.error().context.empty());
}

TEST_CASE("the sound named is the first one the chain cannot pass, not a later one that is also missing") {
  Fixture f;
  f.add("a", {"a"}, 0.5F, 0.5F);
  f.add("u", {"u"}, 0.5F, 0.5F);
  const auto selected = f.select({.meters = &f.project.meterMap()});
  CHECK(!selected);
  if (selected) return;
  const auto& message = selected.error().message;
  CHECK(mentions(message, "sound \"i\""));
  CHECK(!mentions(message, "sound \"u\""));
  CHECK(mentions(message, "bar 1, beat 1"));
}

TEST_CASE("a unit that spans several sounds moves the place where the chain stops") {
  Fixture f;
  f.add("a-i", {"a", "i"}, 0.5F, 0.5F);
  const auto selected = f.select({.meters = &f.project.meterMap()});
  CHECK(!selected);
  if (selected) return;
  CHECK(mentions(selected.error().message, "sound \"u\""));
  CHECK(!mentions(selected.error().message, "sound \"i\""));
}

TEST_CASE("without a meter map the failure still names the sound and the lyric but gives no position") {
  Fixture f;
  f.add("a", {"a"}, 0.5F, 0.5F);
  f.add("i", {"i"}, 0.5F, 0.5F);
  const auto selected = f.select();
  CHECK(!selected);
  if (selected) return;
  CHECK(mentions(selected.error().message, "sound \"u\""));
  CHECK(mentions(selected.error().message, "lyric \"あ\""));
  CHECK(!mentions(selected.error().message, "bar "));
}

TEST_CASE("a voicebank with several styles says which style had no unit") {
  Fixture f;
  f.add("a", {"a"}, 0.5F, 0.5F);
  f.add("i", {"i"}, 0.5F, 0.5F);
  f.bank.styles = {"original"};
  const auto single = f.select();
  CHECK(!single);
  if (single) return;
  CHECK(!mentions(single.error().message, "style"));
  f.bank.styles = {"original", "growl"};
  const auto several = f.select();
  CHECK(!several);
  if (several) return;
  CHECK(mentions(several.error().message, "style \"original\""));
}

TEST_CASE("a very long lyric is shortened in the failure message") {
  Fixture f;
  f.add("a", {"a"}, 0.5F, 0.5F);
  f.add("i", {"i"}, 0.5F, 0.5F);
  f.project.findRegion(f.region)->lyrics.back().surface = std::u32string(200U, U'あ');
  const auto selected = f.select();
  CHECK(!selected);
  if (selected) return;
  CHECK(selected.error().message.size() < 200U);
  CHECK(mentions(selected.error().message, "\u2026"));
  CHECK(mentions(selected.error().message, "sound \"u\""));
}

TEST_CASE("the position counts from where the region begins, not from its own first tick") {
  Fixture f;
  f.add("a", {"a"}, 0.5F, 0.5F);
  f.add("i", {"i"}, 0.5F, 0.5F);
  f.project.findRegion(f.region)->startTick = time::Tick{3840};  // The second 4/4 bar.
  const auto selected = f.select({.meters = &f.project.meterMap()});
  CHECK(!selected);
  if (selected) return;
  CHECK(mentions(selected.error().message, "bar 2, beat 2"));
}

TEST_CASE("the lyric named is the one that owns the sound, which a shared lyric leaves on another note") {
  Fixture f;
  f.add("a", {"a"}, 0.5F, 0.5F);
  f.add("i", {"i"}, 0.5F, 0.5F);
  auto* region = f.project.findRegion(f.region);
  region->lyrics[2].surface = U"う";
  f.tokens[2].lyricOwner = region->lyrics[0].id;
  const auto selected = f.select();
  CHECK(!selected);
  if (selected) return;
  CHECK(mentions(selected.error().message, "lyric \"あ\""));
  CHECK(!mentions(selected.error().message, "う"));
}

TEST_CASE("a render snapshot that cannot be built for want of a unit says which note lacks it") {
  Fixture f;
  f.add("a", {"a"}, 0.5F, 0.5F);
  f.add("i", {"i"}, 0.5F, 0.5F);
  const auto built = f.snapshot();
  CHECK(!built);
  if (built) return;
  CHECK(built.error().code == core::ErrorCode::NotFound);
  CHECK(mentions(built.error().message, "sound \"u\""));
  CHECK(mentions(built.error().message, "lyric \"あ\""));
  CHECK(mentions(built.error().message, "bar 1, beat 2"));
}
