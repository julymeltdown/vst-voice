#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/file_io.hpp"
#include "seam/standalone/eula_acceptance.hpp"

#include <string>

TEST_CASE("EULA acceptance persists only the document identity and UTC timestamp") {
  const auto root = seam::test::support::temporaryDirectory("eula-acceptance");
  const auto path = root / "eula-acceptance.json";
  const seam::standalone::EulaAcceptanceRecord record{
      .documentVersion = "external-beta-eula-1.0",
      .documentSha256 = std::string(64U, 'a'),
      .acceptedAtUtc = "2026-08-22T00:00:00Z"};
  CHECK(seam::standalone::EulaAcceptanceStore::save(path, record));
  auto loaded = seam::standalone::EulaAcceptanceStore::load(path);
  CHECK(loaded);
  CHECK(loaded.value().has_value());
  CHECK(loaded.value()->documentVersion == record.documentVersion);
  CHECK(seam::standalone::EulaAcceptanceStore::matches(
      *loaded.value(), record.documentVersion, record.documentSha256));
  CHECK(!seam::standalone::EulaAcceptanceStore::matches(
      *loaded.value(), record.documentVersion, std::string(64U, 'b')));
}

TEST_CASE("EULA acceptance loader fails closed on malformed state") {
  const auto root = seam::test::support::temporaryDirectory("eula-invalid");
  const auto path = root / "eula-acceptance.json";
  CHECK(seam::core::durableAtomicWriteText(path, "not-json"));
  auto loaded = seam::standalone::EulaAcceptanceStore::load(path);
  CHECK(!loaded);
}

TEST_CASE("EULA acceptance loader rejects extra fields and noncanonical digests") {
  const auto root = seam::test::support::temporaryDirectory("eula-extra-fields");
  const auto path = root / "eula-acceptance.json";
  CHECK(seam::core::durableAtomicWriteText(
      path,
      R"({"documentVersion":"external-beta-eula-1.0","documentSha256":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA","acceptedAtUtc":"2026-08-22T00:00:00Z","private":"must-not-be-stored"})"));
  auto loaded = seam::standalone::EulaAcceptanceStore::load(path);
  CHECK(!loaded);
}
