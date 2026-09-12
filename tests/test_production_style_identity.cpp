#include "test_framework.hpp"
#include "seam/voicebank_production/project.hpp"

#include <set>

namespace production = seam::voicebank_production;

TEST_CASE("production assignment identity agrees with the Python draft inventory") {
  const production::ProductionUnitIdentity identity{"ja", "neutral", "sustain:a", 60};
  CHECK(production::productionUnitIdentitySha256(identity) ==
      "c6eea096f83ec23ee63a29980504370c9c18da78e9312e56641ce5ce77be91a8");
  CHECK(production::productionUnitIdentitySha256({"ja", "柔らかい \"A\"", "sustain:a", 60}) ==
      "35700829e14437ae6c67c43592a6517ea547811148bf31f460a256c42e42a17a");
}

TEST_CASE("production assignment identity retains all four axes without style slug collisions") {
  const production::ProductionUnitIdentity base{"ja", "soft bright", "sustain:a", 60};
  std::set<production::ProductionUnitIdentity> identities{base};
  std::set<std::string> hashes{production::productionUnitIdentitySha256(base)};
  for (const auto& identity : {
      production::ProductionUnitIdentity{"en", "soft bright", "sustain:a", 60},
      production::ProductionUnitIdentity{"ja", "soft-bright", "sustain:a", 60},
      production::ProductionUnitIdentity{"ja", "soft bright", "sustain:i", 60},
      production::ProductionUnitIdentity{"ja", "soft bright", "sustain:a", 66}}) {
    CHECK(identity != base);
    CHECK(identities.insert(identity).second);
    CHECK(hashes.insert(production::productionUnitIdentitySha256(identity)).second);
  }
  CHECK(identities.size() == 5U);
}
