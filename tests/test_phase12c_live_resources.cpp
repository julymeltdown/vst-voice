#include "test_framework.hpp"

#include "seam/live_voice/live_resources.hpp"

#include <filesystem>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required for live resource tests
#endif

TEST_CASE("trusted voicebank promotion builds bounded immutable live resources") {
  seam::voicebank::VoicebankCatalog catalog;
  const auto scanned = catalog.scan({seam::voicebank::VoicebankSearchRoot{
      .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
      .kind = seam::voicebank::VoicebankRootKind::Development,
  }});
  CHECK(scanned);
  CHECK(scanned.value().size() == 1U);
  const auto resource = seam::live_voice::buildTrustedResource(scanned.value().front());
  CHECK(resource);
  CHECK(resource.value()->valid());
  CHECK(resource.value()->trusted);
  CHECK(resource.value()->contentHash == scanned.value().front().contentHash);
  CHECK(resource.value()->units.size() == scanned.value().front().manifest.units.size());
  CHECK(resource.value()->bytes() <= seam::phase12c::kMaxResourceBytes);

  auto untrusted = scanned.value().front();
  untrusted.trust = seam::voicebank::VoicebankTrust::UntrustedInstalled;
  CHECK(!seam::live_voice::buildTrustedResource(untrusted));
}

TEST_CASE("live resource promotion rejects hostile markers and limits") {
  seam::voicebank::VoicebankCatalog catalog;
  const auto scanned = catalog.scan({seam::voicebank::VoicebankSearchRoot{
      .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
      .kind = seam::voicebank::VoicebankRootKind::Development,
  }});
  CHECK(scanned);
  CHECK(!scanned.value().empty());

  auto invalidMarkers = scanned.value().front();
  invalidMarkers.manifest.units.front().markers.loopStart =
      invalidMarkers.manifest.units.front().markers.audioOffset;
  invalidMarkers.manifest.units.front().markers.loopEnd =
      invalidMarkers.manifest.units.front().markers.audioOffset + 1;
  CHECK(!seam::live_voice::buildTrustedResource(invalidMarkers));

  auto invalidPath = scanned.value().front();
  invalidPath.manifest.units.front().audioPath = "../outside.wav";
  CHECK(!seam::live_voice::buildTrustedResource(invalidPath));

  auto requireRelease = scanned.value().front();
  CHECK(!seam::live_voice::buildTrustedResource(
      requireRelease, seam::live_voice::ResourceBuildOptions{
          .requireRelease = true,
      }));
  CHECK(!seam::live_voice::buildTrustedResource(
      scanned.value().front(), seam::live_voice::ResourceBuildOptions{
          .maximumBytes = 43U,
      }));
}
