#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/native_ui/arrangement_panel.hpp"
#include "seam/native_ui/new_project_dialog.hpp"
#include "seam/native_ui/track_inspector.hpp"
#include "seam/authoring/project_lifecycle.hpp"
#include "seam/application/project_factory.hpp"

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
