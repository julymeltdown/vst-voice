#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/project_lifecycle.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/formats/project_json.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

seam::voicebank::VoicebankCandidate candidate() {
  auto manifest = seam::test::support::makeManifest({});
  manifest.id = "demo.voice";
  manifest.version = "2.1.0";
  manifest.displayName = "Demo Voice";
  return seam::voicebank::VoicebankCandidate{
      .manifest = std::move(manifest),
      .bankRoot = "/tmp/demo-voice",
      .contentHash = "0123456789abcdef",
      .trust = seam::voicebank::VoicebankTrust::TrustedInstalled,
      .packageDigest = "package-digest",
      .signerKeyId = "test-signer",
  };
}

seam::authoring::ProjectDocument makeDocument() {
  seam::application::ProjectFactory factory{500U};
  auto project = factory.createProject("Existing");
  const auto trackId = factory.addVocalTrack(project, "Voice");
  static_cast<void>(factory.addRegion(project, trackId, "Region",
                                      seam::time::Tick{0},
                                      seam::time::Tick{3840}));
  return seam::authoring::ProjectDocument{
      std::move(project),
      seam::application::ProjectFactory{factory.nextIdValue()}};
}

}  // namespace

TEST_CASE("project_lifecycle_new_project_creates_valid_canonical_document") {
  auto document = makeDocument();
  seam::authoring::ProjectLifecycleService lifecycle;
  const auto voice = candidate();

  const auto result = lifecycle.createNew(
      document,
      seam::authoring::NewProjectRequest{
          .name = "  新しい曲  ",
          .tempoBpm = 150.0,
          .sampleRate = 44100U,
          .outputChannels = 4U,
          .initialVoicebank = voice,
      });

  CHECK(result);
  const auto& project = document.session().project();
  CHECK(project.name() == "新しい曲");
  CHECK_NEAR(project.tempoMap().bpmAt(seam::time::Tick{0}), 150.0, 1e-9);
  CHECK_NEAR(project.settings().sampleRate, 44100.0, 1e-9);
  CHECK(project.settings().characterDisplay ==
        seam::domain::CharacterDisplayMode::Minimal);
  CHECK(project.routing().deviceOutputChannels == 4U);
  CHECK(project.routing().buses.size() == 1U);
  CHECK(project.routing().buses.front().channelCount == 4U);
  CHECK(project.vocalTracks().size() == 1U);
  CHECK(project.vocalTracks().front().regions.size() == 1U);
  CHECK(project.vocalTracks().front().regions.front().durationTick ==
        seam::time::Tick{16 * 4 * seam::time::kDefaultPpq});
  CHECK(project.vocalTracks().front().voicebank.id == voice.manifest.id);
  CHECK(project.vocalTracks().front().voicebank.version == voice.manifest.version);
  CHECK(project.vocalTracks().front().voicebank.contentHash == voice.contentHash);
  CHECK(document.dirty());
  CHECK(!document.identity().projectPath.has_value());
  CHECK(project.validate());
}

TEST_CASE("project_lifecycle_new_project_rejects_invalid_bounds_without_mutation") {
  const auto requests = std::vector<seam::authoring::NewProjectRequest>{
      {.name = "  ", .tempoBpm = 120.0, .sampleRate = 48000U,
       .outputChannels = 2U, .initialVoicebank = std::nullopt},
      {.name = "Invalid UTF8 \xC0\xAF", .tempoBpm = 120.0,
       .sampleRate = 48000U, .outputChannels = 2U, .initialVoicebank = std::nullopt},
      {.name = "Tempo low", .tempoBpm = 19.99, .sampleRate = 48000U,
       .outputChannels = 2U, .initialVoicebank = std::nullopt},
      {.name = "Tempo high", .tempoBpm = 400.01, .sampleRate = 48000U,
       .outputChannels = 2U, .initialVoicebank = std::nullopt},
      {.name = "Sample rate", .tempoBpm = 120.0, .sampleRate = 88200U,
       .outputChannels = 2U, .initialVoicebank = std::nullopt},
      {.name = "Channels", .tempoBpm = 120.0, .sampleRate = 48000U,
       .outputChannels = 3U, .initialVoicebank = std::nullopt},
  };

  for (const auto& request : requests) {
    auto document = makeDocument();
    const auto before = document.session().project();
    seam::authoring::ProjectLifecycleService lifecycle;
    const auto result = lifecycle.createNew(document, request);
    CHECK(!result);
    CHECK(document.session().project() == before);
    CHECK(!document.dirty());
  }
}

TEST_CASE("project_lifecycle_save_as_round_trips_unicode_and_technical_state") {
  const auto root = seam::test::support::temporaryDirectory("lifecycle-save");
  const auto path = root / "曲 프로젝트.seam";
  auto document = makeDocument();
  seam::authoring::ProjectLifecycleService lifecycle;
  CHECK(lifecycle.createNew(document, seam::authoring::NewProjectRequest{
      .name = "夜の歌 프로젝트",
      .tempoBpm = 175.0,
      .sampleRate = 96000U,
      .outputChannels = 8U,
      .initialVoicebank = candidate(),
  }));

  auto& project = document.session().project();
  auto& region = project.vocalTracks().front().regions.front();
  auto [lyric, note] = document.factory().makeNote(
      seam::time::Tick{960}, seam::time::Tick{720}, 64U, U"きょう",
      seam::domain::Language::Japanese);
  const auto noteId = note.id;
  region.lyrics.push_back(std::move(lyric));
  region.notes.push_back(std::move(note));
  region.phonemeOverrides.push_back(seam::domain::PhonemeOverride{
      .key = seam::domain::PhonemeKey{noteId, 0U},
      .symbol = std::string{"ky"},
      .timing = seam::domain::PhonemeTiming{
          .startOffset = seam::time::Microseconds{-50000},
          .endOffset = seam::time::Microseconds{0},
      },
      .locked = true,
  });
  region.unitSelectionOverrides.push_back(seam::domain::UnitSelectionOverride{
      .startKey = seam::domain::PhonemeKey{noteId, 0U},
      .tokenCount = 1U,
      .unitId = "ky-o-02",
      .renderer = seam::domain::UnitRendererKind::ClassicPsola,
      .locked = true,
  });
  region.seamOverrides.push_back(seam::domain::SeamOverride{
      .incomingStartKey = seam::domain::PhonemeKey{noteId, 0U},
      .seamAmount = 0.75F,
      .overlap = seam::time::Microseconds{9000},
      .phaseReset = 0.8F,
      .envelopeBlend = 0.3F,
      .curve = seam::domain::SeamCurve::HardCharacter,
      .locked = true,
  });
  CHECK(region.pitchAutomation.upsert(seam::domain::PitchAutomationPoint{
      .tick = seam::time::Tick{960},
      .cents = 22.0F,
      .interpolation = seam::domain::CurveInterpolation::Smooth,
  }));
  document.synchronizeDirtyState();

  CHECK(lifecycle.saveAs(document, path));
  CHECK(!document.dirty());
  CHECK(document.identity().projectPath == path);
  CHECK(std::filesystem::exists(path));

  auto target = makeDocument();
  auto opened = lifecycle.open(target, path);
  CHECK(opened);
  CHECK(target.session().project() == document.session().project());
  CHECK(target.identity().projectPath == path);
  CHECK(!target.dirty());
}

TEST_CASE("project_lifecycle_save_requires_path_and_failure_preserves_dirty_state") {
  auto document = makeDocument();
  seam::authoring::ProjectLifecycleService lifecycle;
  CHECK(lifecycle.createNew(document, seam::authoring::NewProjectRequest{
      .name = "Unsaved",
      .tempoBpm = 120.0,
      .sampleRate = 48000U,
      .outputChannels = 2U,
      .initialVoicebank = std::nullopt,
  }));
  const auto noPath = lifecycle.save(document);
  CHECK(!noPath);
  CHECK(noPath.error().code == seam::core::ErrorCode::InvalidState);
  CHECK(document.dirty());

  const auto root = seam::test::support::temporaryDirectory("lifecycle-fault");
  const auto path = root / "fault.seam";
  const auto failed = lifecycle.saveAs(
      document, path,
      seam::authoring::ProjectSaveOptions{
          .faultInjector = [](seam::core::AtomicWriteStage stage) {
            if (stage == seam::core::AtomicWriteStage::BeforeReplace) {
              return seam::core::failure(seam::core::ErrorCode::IoError,
                                         "injected save failure");
            }
            return seam::core::success();
          },
      });
  CHECK(!failed);
  CHECK(document.dirty());
  CHECK(!document.identity().projectPath.has_value());
  CHECK(!std::filesystem::exists(path));
}

TEST_CASE("project_lifecycle_failed_open_preserves_current_document") {
  auto document = makeDocument();
  const auto before = document.session().project();
  seam::authoring::ProjectLifecycleService lifecycle;
  const auto root = seam::test::support::temporaryDirectory("lifecycle-open");
  const auto future = root / "future.seam";
  {
    std::ofstream stream(future);
    stream << R"({"schemaVersion":999})";
  }
  const auto result = lifecycle.open(document, future);
  CHECK(!result);
  CHECK(document.session().project() == before);
  CHECK(!document.identity().projectPath.has_value());
  CHECK(!document.dirty());
}

TEST_CASE("project_lifecycle_never_serializes_active_document_paths") {
  const auto root = seam::test::support::temporaryDirectory("lifecycle-path-boundary");
  const auto path = root / "private-location" / "song.seam";
  auto document = makeDocument();
  seam::authoring::ProjectLifecycleService lifecycle;
  CHECK(lifecycle.createNew(document, seam::authoring::NewProjectRequest{
      .name = "Path Boundary",
      .tempoBpm = 120.0,
      .sampleRate = 48000U,
      .outputChannels = 2U,
      .initialVoicebank = std::nullopt,
  }));
  CHECK(lifecycle.saveAs(document, path));
  const auto content = seam::core::readTextFileLimited(path, 64U * 1024U * 1024U);
  CHECK(content);
  CHECK(content.value().find(path.string()) == std::string::npos);
  CHECK(content.value().find(root.string()) == std::string::npos);
}
