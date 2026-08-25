#include "test_framework.hpp"

#include "seam/distribution/trust_policy.hpp"

TEST_CASE("trust policy validation rejects malformed identity and time windows") {
  seam::distribution::UpdateTrustPolicy policy{
      .schemaVersion = 1,
      .purpose = "update-trust-policy",
      .channel = "external-beta",
      .policyEpoch = 1,
      .rootKeyId = "root-key",
      .allowedPlatforms = {"macos-arm64"},
      .issuedAt = "2026-08-22T00:00:00Z",
      .notBefore = "2026-08-21T00:00:00Z",
      .expiresAt = "2026-08-20T00:00:00Z",
      .compromiseCutoff = "2026-08-20T00:00:00Z",
      .delegatedKeys = {},
      .signature = {}};
  CHECK(!seam::distribution::validateUpdateTrustPolicy(policy));
}
