#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/rendering/project_renderer.hpp"
#include "seam/voicebank/wav.hpp"

#include <filesystem>
#include <algorithm>
#include <cmath>

TEST_CASE("production renderer mixes a referenced backing WAV track") {
  const auto root = seam::test::support::temporaryDirectory("media-render");
  const auto media = root / "backing.wav";
  const auto source = seam::test::support::sineWave(48000U, 220.0, 0.2);
  CHECK(seam::voicebank::writeMonoPcm16Wav(media, 48000U, source));

  seam::application::ProjectFactory factory{10U};
  auto project = factory.createProject("Backing");
  seam::application::EditorSession session{project};
  CHECK(session.execute(std::make_unique<seam::application::ConfigureProjectOutputCommand>(2U)));
  project = session.project();
  project.audioTracks().push_back(seam::domain::AudioTrack{
      .id = seam::domain::TrackId{900U},
      .name = "Backing",
      .mediaPath = media.string(),
      .startTick = seam::time::Tick{0},
      .gainDb = 0.0F,
      .pan = 0.0F,
      .muted = false,
      .solo = false,
      .outputRoute = seam::domain::TrackOutputRoute{
          .bus = seam::domain::BusId{1U},
          .matrix = seam::domain::RoutingMatrix::monoToStereo()},
  });

  seam::rendering::ProductionProjectRenderer renderer;
  const auto rendered = renderer.render(
      project, {}, seam::domain::TrackId{}, seam::domain::RegionId{}, 1U,
      48000U);
  CHECK(rendered);
  CHECK(rendered.value().trackCount == 1U);
  CHECK(!rendered.value().interleaved.empty());
  CHECK(std::any_of(rendered.value().interleaved.begin(),
                   rendered.value().interleaved.end(),
                   [](float value) { return value != 0.0F; }));

  project.audioTracks().front().pan = 1.0F;
  const auto panned = renderer.render(
      project, {}, seam::domain::TrackId{}, seam::domain::RegionId{}, 2U,
      48000U);
  CHECK(panned);
  CHECK(panned.value().interleaved.size() >= 2U);
  bool rightDominant = false;
  for (std::size_t index = 0U; index + 1U < panned.value().interleaved.size();
       index += 2U) {
    if (std::abs(panned.value().interleaved[index + 1U]) >
        std::abs(panned.value().interleaved[index]) + 1.0e-5F) {
      rightDominant = true;
      break;
    }
  }
  CHECK(rightDominant);

  project.audioTracks().front().mediaPath = (root / "missing.wav").string();
  const auto missing = renderer.render(
      project, {}, seam::domain::TrackId{}, seam::domain::RegionId{}, 3U,
      48000U);
  CHECK(!missing);
  CHECK(missing.error().code == seam::core::ErrorCode::NotFound ||
        missing.error().code == seam::core::ErrorCode::IoError);
  project.audioTracks().front().mediaPath = media.string();
  project.audioTracks().front().mediaHash = std::string(64U, '0');
  const auto mismatched = renderer.render(
      project, {}, seam::domain::TrackId{}, seam::domain::RegionId{}, 4U,
      48000U);
  CHECK(!mismatched);
  CHECK(mismatched.error().code == seam::core::ErrorCode::Conflict);
}

TEST_CASE("production renderer applies backing media trim before routing") {
  const auto root = seam::test::support::temporaryDirectory("media-render-trim");
  const auto media = root / "backing.wav";
  const std::vector<float> source{0.0F, 0.1F, 0.2F, 0.3F,
                                  0.4F, 0.5F, 0.6F, 0.7F};
  CHECK(seam::voicebank::writeMonoPcm16Wav(media, 48000U, source));

  seam::application::ProjectFactory factory{10U};
  auto project = factory.createProject("Trimmed backing");
  seam::application::EditorSession session{project};
  CHECK(session.execute(std::make_unique<seam::application::ConfigureProjectOutputCommand>(2U)));
  project = session.project();
  project.audioTracks().push_back(seam::domain::AudioTrack{
      .id = seam::domain::TrackId{901U},
      .name = "Trimmed backing",
      .mediaPath = media.string(),
      .trimStartFrame = 2U,
      .trimEndFrame = 6U,
      .startTick = seam::time::Tick{0},
      .outputRoute = seam::domain::TrackOutputRoute{
          .bus = seam::domain::BusId{1U},
          .matrix = seam::domain::RoutingMatrix::monoToStereo()},
  });

  seam::rendering::ProductionProjectRenderer renderer;
  const auto rendered = renderer.render(
      project, {}, seam::domain::TrackId{}, seam::domain::RegionId{}, 1U,
      48000U);
  CHECK(rendered);
  CHECK(rendered.value().interleaved.size() == 8U);
  CHECK_NEAR(rendered.value().interleaved[0], 0.1414, 0.02);
}
