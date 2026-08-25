#include "test_framework.hpp"

#include "seam/live_voice/articulation.hpp"
#include "seam/live_voice/live_resources.hpp"
#include "seam/voicebank/catalog.hpp"

#include <filesystem>
#include <string>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required for live articulation tests
#endif

namespace {

seam::core::Result<std::shared_ptr<const seam::live_voice::LiveVoicebankResources>>
loadResources() {
  seam::voicebank::VoicebankCatalog catalog;
  const auto scanned = catalog.scan({seam::voicebank::VoicebankSearchRoot{
      .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
      .kind = seam::voicebank::VoicebankRootKind::Development,
  }});
  if (!scanned || scanned.value().empty()) {
    return seam::core::failure<
        std::shared_ptr<const seam::live_voice::LiveVoicebankResources>>(
        seam::core::ErrorCode::NotFound,
        "Live articulation fixture did not produce a Voicebank candidate");
  }
  return seam::live_voice::LiveResourceBuilder{}.build(scanned.value().front());
}

}

TEST_CASE("production live resources preserve exact identity and bounded PCM") {
  const auto resources = loadResources();
  CHECK(resources);
  CHECK(resources.value()->identity.id ==
        "demo.public-domain.human.production");
  CHECK(resources.value()->identity.contentHash.size() == 64U);
  CHECK(!resources.value()->units.empty());
  CHECK(resources.value()->decodedBytes > 0U);
  CHECK(resources.value()->decodedBytes <= seam::phase12c::kMaxResourceBytes);
}

TEST_CASE("articulation planning is deterministic and reports transition fallback") {
  const auto resources = loadResources();
  CHECK(resources);
  seam::live_voice::ArticulationPlanner planner;
  const seam::live_voice::ArticulationRequest request{
      .previousVowel = "e",
      .targetVowel = "o",
      .key = 60,
      .previousKey = 62,
      .legato = true,
  };
  const auto first = planner.plan(*resources.value(), request);
  CHECK(first);
  CHECK(first.value().sustain.unit != nullptr);
  CHECK(first.value().sustain.unit->unitId == "demo.ja.g4.o.01");
  CHECK(first.value().attack.unit != nullptr);
  CHECK(first.value().usedTransitionFallback);

  for (int iteration = 0; iteration < 1000; ++iteration) {
    const auto next = planner.plan(*resources.value(), request);
    CHECK(next);
    CHECK(next.value().sustain.unit->unitId ==
          first.value().sustain.unit->unitId);
    CHECK(next.value().attack.unit->unitId ==
          first.value().attack.unit->unitId);
  }
}

TEST_CASE("articulation planning rejects a missing sustain vowel") {
  const auto resources = loadResources();
  CHECK(resources);
  seam::live_voice::ArticulationPlanner planner;
  const auto result = planner.plan(
      *resources.value(), seam::live_voice::ArticulationRequest{
                              .targetVowel = "missing",
                              .key = 60,
                          });
  CHECK(!result);
}
