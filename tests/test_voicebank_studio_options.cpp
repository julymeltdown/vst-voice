#include "test_framework.hpp"

#include "options.hpp"

#include <string>
#include <vector>

namespace {

std::optional<seam::voicebank_studio_native::Options> parse(
    std::vector<std::string> arguments) {
  std::vector<char*> values;
  values.reserve(arguments.size());
  for (auto& argument : arguments) values.push_back(argument.data());
  return seam::voicebank_studio_native::parseOptions(
      static_cast<int>(values.size()), values.data());
}

std::vector<std::string> productionArguments() {
  return {
      "seam_voicebank_studio_native",
      "--production-project", "workspace",
      "--inventory-sha256", std::string(64U, 'a'),
      "--operator-id", "producer-a",
  };
}

}

TEST_CASE("voicebank studio options reject numeric suffixes") {
  auto peak = productionArguments();
  peak.insert(peak.end(), {"--operation", "normalize", "--target-peak",
                           "0.8junk"});
  CHECK(!parse(std::move(peak)));

  auto index = productionArguments();
  index.insert(index.end(), {"--production-unit-index", "2junk"});
  CHECK(!parse(std::move(index)));
}

TEST_CASE("voicebank studio options retain exact valid operation values") {
  auto arguments = productionArguments();
  arguments.insert(arguments.end(), {"--operation", "normalize",
                                     "--target-peak", "0.8"});
  const auto options = parse(std::move(arguments));
  CHECK(options.has_value());
  CHECK(options->operationKind ==
        seam::voicebank_production::OperationKind::NormalizeGain);
  CHECK_NEAR(options->targetPeak, 0.8, 1e-6);
}
