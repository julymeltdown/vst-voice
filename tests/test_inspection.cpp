#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/synthesis/phrase_renderer.hpp"
#include "seam/synthesis/timing_solver.hpp"
#include "seam/synthesis/unit_selection.hpp"
#include "seam/ui/sample_microscope_model.hpp"
#include "seam/ui/timeline_transform.hpp"
#include "seam/ui/unit_lane_model.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

TEST_CASE("unit lane exposes actual renderer fallback seam and alternatives") {
  seam::application::ProjectFactory factory{1700};
  auto project = factory.createProject("Unit lane");
  const auto trackId = factory.addVocalTrack(project, "Voice");
  const auto regionId = factory.addRegion(
      project, trackId, "Phrase", seam::time::Tick{0}, seam::time::Tick{7680});
  auto [lyric, note] = factory.makeNote(
      seam::time::Tick{960}, seam::time::Tick{1920}, 69, U"か",
      seam::domain::Language::Japanese);
  auto* region = project.findRegion(regionId);
  region->lyrics.push_back(std::move(lyric));
  region->notes.push_back(std::move(note));
  region->sortNotes();

  seam::phonemizer::JapaneseKanaPhonemizer phonemizer;
  const auto phonemes = phonemizer.phonemize(*region);
  auto unit = seam::test::support::makeUnit(
      "k-a-main", {"k", "a"}, "audio/k-a.wav", 69,
      seam::voicebank::UnitKind::Cv, 24000);
  auto alternate = unit;
  alternate.id = "k-a-alt";
  alternate.take = 2;
  const auto manifest = seam::test::support::makeManifest({unit, alternate});
  seam::synthesis::DeterministicUnitSelector selector;
  const auto plan = selector.select(manifest, *region, phonemes.tokens, "original");
  CHECK(plan);
  seam::synthesis::TimingSolver solver;
  const auto timing = solver.solve(project, *region, phonemes.tokens,
                                   plan.value(), manifest, 48000);
  CHECK(timing);
  seam::synthesis::PhraseRenderResult renderInfo;
  renderInfo.placements.push_back(seam::synthesis::RenderedPlacementInfo{
      .unitId = "k-a-main",
      .requestedStart = timing.value().placements.front().destinationStart,
      .alignedStart = timing.value().placements.front().destinationStart,
      .frameCount = 24000,
      .vowelOnset = timing.value().placements.front().desiredVowelOnset,
      .requestedRenderer = seam::voicebank::RendererHint::SpectralClassic,
      .actualRenderer = seam::voicebank::RendererHint::Raw,
      .usedFallback = true,
      .forcedSelection = false,
      .seamAmount = 0.88F,
      .seamCurve = seam::domain::SeamCurve::HardCharacter,
      .diagnostic = "fallback",
  });

  seam::ui::TimelineTransform timeline{project.ppq(), 120.0, seam::time::Tick{0}};
  seam::ui::UnitLaneModel model;
  model.rebuild(project, *region, phonemes, plan.value(), timing.value(),
                &renderInfo, timeline, 72.0, 400.0, 32.0, 48000);
  CHECK(model.visuals().size() == 1);
  const auto& visual = model.visuals().front();
  CHECK(visual.unitId == "k-a-main");
  CHECK(visual.alternatives == (std::vector<std::string>{"k-a-alt"}));
  CHECK(visual.usedFallback);
  CHECK(visual.actualRenderer == seam::voicebank::RendererHint::Raw);
  CHECK_NEAR(visual.seamAmount, 0.88, 1.0e-6);
  CHECK(model.hitTest(seam::ui::Point{
      visual.bounds.x + visual.bounds.width * 0.5,
      visual.bounds.y + visual.bounds.height * 0.5,
  }).has_value());
}

TEST_CASE("sample microscope maps waveform markers pitch marks and hit tests") {
  constexpr std::uint32_t sampleRate = 48000;
  const auto samples = seam::test::support::sineWave(sampleRate, 220.0, 0.5);
  seam::voicebank::AudioBuffer source{
      .sampleRate = sampleRate,
      .channels = 1,
      .interleaved = samples,
  };
  auto unit = seam::test::support::makeUnit(
      "microscope-a", {"a"}, "audio/a.wav", 57,
      seam::voicebank::UnitKind::Sustain, samples.size());
  for (seam::time::SampleFrame frame = 5000; frame < 19000; frame += 218) {
    unit.pitchMarks.push_back(seam::voicebank::PitchMark{
        .frame = frame,
        .confidence = 0.95F,
        .locked = frame == 5000,
    });
  }
  seam::ui::SampleMicroscopeModel model;
  const auto rebuilt = model.rebuild(
      unit, source,
      seam::ui::Rect{20.0, 40.0, 800.0, 180.0},
      seam::ui::Rect{20.0, 230.0, 800.0, 220.0},
      400,
      seam::voicebank::SpectrogramConfig{
          .fftSize = 512,
          .hopSize = 128,
          .minimumDb = -90.0F,
          .maximumDb = -6.0F,
      });
  CHECK(rebuilt);
  CHECK(!model.waveform().empty());
  CHECK(model.spectrogram().columns > 0);
  CHECK(model.markers().size() >= 6);
  CHECK(model.pitchMarks().size() == unit.pitchMarks.size());
  const auto vowelX = model.frameToPixel(unit.markers.vowelOnset);
  const auto marker = model.hitTestMarker(
      seam::ui::Point{vowelX + 1.0, 100.0}, 3.0);
  CHECK(marker.has_value());
  CHECK(*marker == seam::ui::AcousticMarkerKind::VowelOnset);
  const auto pitchX = model.pitchMarks().front().x;
  const auto pitchMark = model.hitTestPitchMark(
      seam::ui::Point{pitchX, 120.0}, 2.0);
  CHECK(pitchMark.has_value());
  CHECK(*pitchMark == 0U);
  CHECK(std::abs(model.pixelToFrame(model.frameToPixel(12000)) - 12000) <= 1);

  const auto movedVowel = seam::time::SampleFrame{3900};
  CHECK(model.moveMarker(
      unit, seam::ui::AcousticMarkerKind::VowelOnset,
      model.frameToPixel(movedVowel),
      static_cast<seam::time::SampleFrame>(samples.size())));
  CHECK(unit.markers.vowelOnset == movedVowel);
  const auto vowelVisual = std::find_if(
      model.markers().begin(), model.markers().end(), [](const auto& value) {
        return value.kind == seam::ui::AcousticMarkerKind::VowelOnset;
      });
  CHECK(vowelVisual != model.markers().end());
  CHECK(vowelVisual->frame == movedVowel);

  const auto movedPitch = unit.pitchMarks[1].frame + 7;
  CHECK(model.movePitchMark(
      unit, 1U, model.frameToPixel(movedPitch)));
  CHECK(unit.pitchMarks[1].frame == movedPitch);
  CHECK(model.pitchMarks()[1].frame == movedPitch);
}

TEST_CASE("sample microscope applies validated marker and pitch-mark edits") {
  constexpr std::uint32_t sampleRate = 48000;
  const auto samples = seam::test::support::sineWave(sampleRate, 220.0, 0.5);
  seam::voicebank::AudioBuffer source{
      .sampleRate = sampleRate,
      .channels = 1,
      .interleaved = samples,
  };
  auto unit = seam::test::support::makeUnit(
      "microscope-edit-a", {"a"}, "audio/a.wav", 57,
      seam::voicebank::UnitKind::Sustain, samples.size());
  for (seam::time::SampleFrame frame = unit.markers.stableStart;
       frame < unit.markers.releaseStart.value_or(unit.markers.audioEnd);
       frame += 218) {
    unit.pitchMarks.push_back(seam::voicebank::PitchMark{
        .frame = frame, .confidence = 0.96F, .locked = false});
  }
  seam::ui::SampleMicroscopeModel model;
  CHECK(model.rebuild(
      unit, source,
      seam::ui::Rect{20.0, 40.0, 800.0, 180.0},
      seam::ui::Rect{20.0, 230.0, 800.0, 220.0}, 400,
      seam::voicebank::SpectrogramConfig{
          .fftSize = 512, .hopSize = 128,
          .minimumDb = -90.0F, .maximumDb = -6.0F}));

  const auto newVowel = unit.markers.vowelOnset + 160;
  CHECK(model.moveMarker(
      unit, seam::ui::AcousticMarkerKind::VowelOnset,
      model.frameToPixel(newVowel),
      static_cast<seam::time::SampleFrame>(source.frameCount())));
  CHECK(std::abs(unit.markers.vowelOnset - newVowel) <= 1);
  CHECK(std::any_of(model.markers().begin(), model.markers().end(),
                    [newVowel](const auto& marker) {
                      return marker.kind == seam::ui::AcousticMarkerKind::VowelOnset &&
                             std::abs(marker.frame - newVowel) <= 1;
                    }));

  const std::size_t pitchIndex = 3U;
  const auto oldPitch = unit.pitchMarks[pitchIndex].frame;
  CHECK(model.movePitchMark(
      unit, pitchIndex, model.frameToPixel(oldPitch + 8)));
  CHECK(std::abs(unit.pitchMarks[pitchIndex].frame - (oldPitch + 8)) <= 1);
  CHECK(model.pitchMarks()[pitchIndex].frame == unit.pitchMarks[pitchIndex].frame);
}
