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

TEST_CASE("voicebank studio opens a source-free Designer without weakening producer arguments") {
  const auto defaultLaunch=parse({"studio"}); CHECK(defaultLaunch); CHECK(defaultLaunch->startDesigner);
  CHECK(defaultLaunch->manifest.empty()); CHECK(!defaultLaunch->productionProject);
  const auto explicitLaunch=parse({"studio","--designer","--auto-close-ms","100"});
  CHECK(explicitLaunch); CHECK(explicitLaunch->startDesigner);
  CHECK(!parse({"studio","--designer","--record-ms","100"}));
  CHECK(!parse({"studio","--designer","--production-project","workspace"}));
  CHECK(!parse({"studio","--designer","--operator-id","producer"}));
  CHECK(!parse({"studio","--unknown"}));
  CHECK(!parse({"studio","--window-width","720"}));
  const auto withBank=parse({"studio","--designer","--manifest","bank.json"});
  CHECK(withBank); CHECK(withBank->manifest=="bank.json");
  CHECK(!parse(productionArguments())->startDesigner);
}

TEST_CASE("voicebank studio explicit dimensions override restored geometry") {
  CHECK(!parse(productionArguments())->windowSizeSpecified);
  for (const auto* flag : {"--window-width","--window-height"}) {
    auto args=productionArguments(); args.insert(args.end(),{flag,"720"});
    const auto options=parse(std::move(args)); CHECK(options);
    CHECK(options->windowSizeSpecified);
    if (std::string_view{flag}=="--window-width") CHECK(options->windowWidth==720U);
    else CHECK(options->windowHeight==720U);
  }
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
