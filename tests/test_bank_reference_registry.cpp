#include "test_framework.hpp"

#include "seam/authoring/bank_reference_registry.hpp"

TEST_CASE("bank reference registry rejects conflicting content for one version") {
  seam::authoring::BankReferenceRegistry registry;
  const seam::voicebank::VoicebankCandidate first{
      .manifest = seam::voicebank::Manifest{.id = "bank", .version = "1.0"},
      .contentHash = "a"};
  const seam::voicebank::VoicebankCandidate second{
      .manifest = seam::voicebank::Manifest{.id = "bank", .version = "1.0"},
      .contentHash = "b"};
  CHECK(registry.registerCandidate(first));
  CHECK(!registry.registerCandidate(second));
  CHECK(registry.contains("bank", "1.0", "a"));
  CHECK(!registry.contains("bank", "1.0", "b"));
}
