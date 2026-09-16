#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/native_ui/arrangement_panel.hpp"
#include "seam/native_ui/new_project_dialog.hpp"
#include "seam/native_ui/track_inspector.hpp"
#include "seam/authoring/project_lifecycle.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/ui/expression_lane.hpp"

#include <algorithm>
#include <filesystem>

namespace {

seam::voicebank::VoicebankCandidate candidate() {
  auto manifest = seam::test::support::makeManifest({});
  manifest.id = "test.voice";
  manifest.version = "1.0.0";
  manifest.displayName = "Test Voice";
  return seam::voicebank::VoicebankCandidate{
      .manifest = std::move(manifest),
      .bankRoot = "/tmp/test-voice",
      .contentHash = std::string(64U, 'a'),
      .trust = seam::voicebank::VoicebankTrust::TrustedInstalled,
  };
}

// The inspector has to state a channel's applicability where the creator is looking at the track, not
// only inside the automation band. A refusal a creator discovers by drawing a curve that is then
// dropped is a refusal they paid for; this makes it visible before the draw.
TEST_CASE("The inspector states each channel's applicability and its value at the playhead") {
  seam::application::ProjectFactory factory{9200U};
  auto project = factory.createProject("Inspector channels");
  const auto trackId = factory.addVocalTrack(project, "Singer");
  const auto regionId = factory.addRegion(project, trackId, "Phrase", seam::time::Tick{0},
                                          seam::time::Tick{3840});
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{3840}, 60U, U"あ",
                                        seam::domain::Language::Japanese);
  auto* region = project.findRegion(regionId);
  region->lyrics.push_back(std::move(lyric));
  region->notes.push_back(std::move(note));

  // A saved bank singer with no designer-owned excitation or tract. Every timbral channel is refused,
  // and each refusal has to be visible in the inspector even though no curve is stored yet, because the
  // whole point is to answer the question before the creator draws.
  const auto bankOnly = seam::native_ui::TrackInspectorModel::snapshot(project, trackId);
  CHECK(bankOnly.valid);
  CHECK(bankOnly.vocal);
  CHECK(!bankOnly.expressionRows.empty());
  // The inspector is a fixed-height panel, so the number of rows is bounded rather than proportional to
  // the channel count.
  CHECK(bankOnly.expressionRows.size() <= 3U);
  for (const auto& row : bankOnly.expressionRows) {
    CHECK(!row.label.empty());
    CHECK(!row.unit.empty());
    CHECK(!row.refusal.empty());
    // The wording has to match the lane's, because two surfaces describing one refusal differently is
    // how a creator learns to distrust both.
    const auto lane = seam::ui::validateExpressionCarrier(project, trackId, row.channel);
    CHECK(!lane.hasValue());
    if (!lane) CHECK(row.refusal == lane.error().message);
  }

  // A stored curve earns a row even on a singer that cannot render it, so a refusal is never hidden by
  // an empty one. The formant row must report the channel's own unit, not a generic range.
  region = project.findRegion(regionId);
  const auto drawn = region->formantAutomation.upsert(
      seam::domain::FormantAutomationPoint{seam::time::Tick{480}, 4.0F});
  CHECK(drawn.hasValue());
  const auto withCurve = seam::native_ui::TrackInspectorModel::snapshot(
      project, trackId, seam::time::Tick{480});
  const auto formant = std::find_if(withCurve.expressionRows.begin(), withCurve.expressionRows.end(),
      [](const auto& row) { return row.channel == seam::ui::ExpressionChannel::Formant; });
  CHECK(formant != withCurve.expressionRows.end());
  if (formant != withCurve.expressionRows.end()) {
    CHECK(formant->storedPoints == 1U);
    CHECK(formant->label == "Formant");
    CHECK(formant->unit == "semitones");
    // The value is read at the playhead the caller named, so the row and the lane agree about what the
    // curve is doing at the position the creator is working at.
    CHECK_NEAR(formant->valueAtPlayhead, 4.0, 1e-6);
  }

  // A shifted playhead reports the curve there, not the stored point's value, so the row cannot claim a
  // value the automation does not hold at the position being displayed.
  const auto elsewhere = seam::native_ui::TrackInspectorModel::snapshot(
      project, trackId, seam::time::Tick{240});
  const auto shifted = std::find_if(elsewhere.expressionRows.begin(), elsewhere.expressionRows.end(),
      [](const auto& row) { return row.channel == seam::ui::ExpressionChannel::Formant; });
  CHECK(shifted != elsewhere.expressionRows.end());
  if (shifted != elsewhere.expressionRows.end()) CHECK_NEAR(shifted->valueAtPlayhead, 4.0, 1e-6);
}

}

TEST_CASE("new project dialog requires explicit choices and preserves cancel") {
  seam::native_ui::NewProjectDialogModel dialog{{candidate()}};
  dialog.setName("Song");
  dialog.setTempoBpm(140.0);
  dialog.setMeter(7U, 8U);
  dialog.setSampleRate(96000U);
  dialog.setOutputChannels(8U);
  dialog.setProjectPath("/tmp/project-seam-new.seam");
  CHECK(dialog.selectVoicebank(0U));
  const auto request = dialog.submit();
  CHECK(request);
  CHECK(request.value().name == "Song");
  CHECK(request.value().tempoBpm == 140.0);
  CHECK(request.value().meterNumerator == 7U);
  CHECK(request.value().meterDenominator == 8U);
  CHECK(request.value().sampleRate == 96000U);
  CHECK(request.value().outputChannels == 8U);
  CHECK(request.value().createInitialVocalTrack);
  CHECK(request.value().initialVoicebank.has_value());
  CHECK(request.value().projectPath ==
        std::filesystem::path{"/tmp/project-seam-new.seam"});

  dialog.cancel();
  CHECK(!dialog.submit());
  dialog.reopen();
  dialog.setCreateInitialVocalTrack(false);
  CHECK(!dialog.selectVoicebank(0U));
  const auto empty = dialog.submit();
  CHECK(empty);
  CHECK(!empty.value().createInitialVocalTrack);
  CHECK(!empty.value().initialVoicebank.has_value());
}

TEST_CASE("arrangement panel and track inspector mirror project structure") {
  seam::application::ProjectFactory factory{9000U};
  auto project = factory.createProject("Arrangement view");
  const auto vocalId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, vocalId, "Verse",
                                           seam::time::Tick{480},
                                           seam::time::Tick{1920});
  project.audioTracks().push_back(seam::domain::AudioTrack{
      .id = seam::domain::TrackId{9100U},
      .name = "Backing",
      .mediaPath = "/tmp/backing.wav",
      .mediaHash = std::string(64U, 'b'),
      .sourceSampleRate = 48000U,
      .sourceChannels = 1U,
      .sourceFrameCount = 1000U,
      .startTick = seam::time::Tick{0},
      .outputRoute = seam::domain::TrackOutputRoute{
          .bus = seam::domain::BusId{1U},
          .matrix = seam::domain::RoutingMatrix::monoToStereo(),
      },
  });

  seam::native_ui::ArrangementPanelModel panel;
  panel.rebuild(project, vocalId, regionId);
  CHECK(panel.tracks().size() == 2U);
  CHECK(panel.tracks().front().selected);
  CHECK(panel.tracks().front().regions.front().selected);
  CHECK(panel.tracks().back().vocal == false);
  CHECK(panel.selectTrack(project, seam::domain::TrackId{9100U}));
  CHECK(panel.selectedTrack() == seam::domain::TrackId{9100U});
  CHECK(!panel.selectedRegion().valid());
  CHECK(panel.selectRegion(project, regionId));
  CHECK(panel.selectedTrack() == vocalId);

  const auto vocal = seam::native_ui::TrackInspectorModel::snapshot(
      project, vocalId);
  CHECK(vocal.valid);
  CHECK(vocal.vocal);
  CHECK(vocal.name == "Lead");
  const auto audio = seam::native_ui::TrackInspectorModel::snapshot(
      project, seam::domain::TrackId{9100U});
  CHECK(audio.valid);
  CHECK(!audio.vocal);
  CHECK(audio.mediaHash == std::string(64U, 'b'));
}
