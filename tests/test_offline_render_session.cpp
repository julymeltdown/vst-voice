#include "test_framework.hpp"

#include "seam/clap_editor/offline_render_session.hpp"

#include <string>

namespace {

seam::clap_editor::OfflineRenderIdentity identity() {
  return seam::clap_editor::OfflineRenderIdentity{
      .projectRevision = 17U,
      .sampleRate = 48000U,
      .quality = seam::rendering::RenderQuality::Final,
      .authority = seam::clap_editor::OfflineTimingAuthority::FixedAudio,
      .projectContentHash = std::string(64U, 'a'),
      .timingMapHash = std::string(64U, 'b'),
      .rendererIdentity = "4.1.0/offline/v1",
  };
}

}  // namespace

TEST_CASE("offline render identity rejects preview and malformed provenance") {
  auto value = identity();
  CHECK(value.validate());
  value.quality = seam::rendering::RenderQuality::Preview;
  CHECK(!value.validate());
  value.quality = seam::rendering::RenderQuality::Final;
  value.projectContentHash[0] = 'A';
  CHECK(!value.validate());
  value.projectContentHash = std::string(63U, 'a');
  CHECK(!value.validate());
}

TEST_CASE("offline render session publishes only the current final identity") {
  seam::clap_editor::OfflineRenderSession session;
  const auto current = identity();
  CHECK(session.begin(current));
  CHECK(session.view().state ==
        seam::clap_editor::OfflineRenderState::Pending);
  auto stale = current;
  stale.projectRevision += 1U;
  CHECK(!session.publish(stale, true));
  CHECK(!session.readyFor(stale));
  CHECK(session.publish(current, true, "final PCM ready"));
  CHECK(session.readyFor(current));
  CHECK(session.view().hasAudio);
  CHECK(session.view().diagnostic == "final PCM ready");
}

TEST_CASE("offline render session fails closed for empty audio and invalidates ready data") {
  seam::clap_editor::OfflineRenderSession session;
  const auto current = identity();
  CHECK(session.begin(current));
  CHECK(!session.publish(current, false));
  CHECK(session.view().state == seam::clap_editor::OfflineRenderState::Failed);
  CHECK(!session.view().hasAudio);
  CHECK(!session.readyFor(current));

  CHECK(session.begin(current));
  CHECK(session.publish(current, true));
  session.invalidate("host timing changed");
  CHECK(session.view().state == seam::clap_editor::OfflineRenderState::Stale);
  CHECK(!session.readyFor(current));
  CHECK(session.view().diagnostic == "host timing changed");
}
