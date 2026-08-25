#include "test_framework.hpp"

#include "seam/native_ui/render_status_panel.hpp"

#include <string>

TEST_CASE("render status panel preserves stale audible revision and actions") {
  seam::native_ui::RenderStatusPanelModel model;
  model.update(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Queued,
      .requestedRevision = 2U,
      .audibleRevision = 1U,
      .hasAudibleAudio = true,
      .diagnostic = "Queued latest revision",
  });
  CHECK(model.view().state == seam::native_ui::RenderStatusState::Queued);
  CHECK(model.view().audibleRevision == 1U);
  CHECK(model.isStale());
  CHECK(model.canCancel());
  CHECK(!model.canRetry());
  CHECK(model.label().find("STALE") != std::string::npos);
  CHECK(model.label().find("REQ r2") != std::string::npos);
  CHECK(model.label().find("AUD r1") != std::string::npos);

  model.update(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Queued,
      .requestedRevision = 1U,
      .audibleRevision = 0U,
      .hasAudibleAudio = true,
      .diagnostic = "Queued latest revision",
  });
  CHECK(model.isStale());
  CHECK(model.label().find("STALE AUDIO") != std::string::npos);

  model.update(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Failed,
      .requestedRevision = 2U,
      .audibleRevision = 1U,
      .hasAudibleAudio = true,
      .diagnostic = "Voicebank is missing",
  });
  CHECK(!model.canCancel());
  CHECK(model.canRetry());
  CHECK(model.view().diagnostic == "Voicebank is missing");

  model.update(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Ready,
      .requestedRevision = 2U,
      .audibleRevision = 2U,
      .hasAudibleAudio = true,
      .diagnostic = "Ready",
  });
  CHECK(!model.isStale());
  CHECK(model.label().find("READY") != std::string::npos);
  CHECK(model.label().find("REQ r2") != std::string::npos);
  CHECK(model.label().find("AUD r2") != std::string::npos);

  model.update(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Ready,
      .requestedRevision = 3U,
      .audibleRevision = 3U,
      .hasAudibleAudio = true,
      .diagnostic = "Render completed with 1 diagnostic(s): Backing media is missing",
  });
  CHECK(model.label().find("Backing media is missing") != std::string::npos);
}

TEST_CASE("render status panel marks same-revision quality replacement stale") {
  seam::native_ui::RenderStatusPanelModel model;
  model.update(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Stale,
      .requestedRevision = 7U,
      .audibleRevision = 7U,
      .hasAudibleAudio = true,
      .requestedQuality = seam::rendering::RenderQuality::Final,
      .audibleQuality = seam::rendering::RenderQuality::Preview,
      .diagnostic = "Previous audio is stale while the final render runs",
  });
  CHECK(model.isStale());
  CHECK(model.label().find("STALE AUDIO") != std::string::npos);
  CHECK(model.label().find("REQ r7 FINAL") != std::string::npos);
  CHECK(model.label().find("AUD r7 PREVIEW") != std::string::npos);
}

TEST_CASE("render status panel honors explicit stale publication truth") {
  seam::native_ui::RenderStatusPanelModel model;
  model.update(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Queued,
      .requestedRevision = 11U,
      .audibleRevision = 11U,
      .requestedQuality = seam::rendering::RenderQuality::Preview,
      .audibleQuality = seam::rendering::RenderQuality::Preview,
      .hasAudibleAudio = true,
      .audibleAudioStale = true,
      .diagnostic = "Coordinator reports stale audible PCM",
  });
  CHECK(model.isStale());
  CHECK(model.label().find("STALE AUDIO") != std::string::npos);
}

TEST_CASE("render status panel exposes active voicebank and renderer context") {
  seam::native_ui::RenderStatusPanelModel model;
  model.update(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Rendering,
      .requestedRevision = 9U,
      .audibleRevision = 8U,
      .hasAudibleAudio = true,
      .activeVoicebankId = "demo.voice",
      .activeVoicebankVersion = "1.2.3",
      .activeRenderer = "classic-psola",
  });
  CHECK(model.view().activeVoicebankId == "demo.voice");
  CHECK(model.view().activeVoicebankVersion == "1.2.3");
  CHECK(model.view().activeRenderer == "classic-psola");
  CHECK(model.label().find("demo.voice") != std::string::npos);
  CHECK(model.label().find("classic-psola") != std::string::npos);
}
