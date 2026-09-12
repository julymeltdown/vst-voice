#include "seam/phase12c/clap_matrix_host.hpp"
#include "seam/core/file_io.hpp"
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>

int main(int argc, char** argv) {
  using namespace seam;
  using Json = formats::JsonValue;
  std::optional<std::filesystem::path> output, plugin, bank;
  bool fixture = false;
  bool outputAdmitted = false;
  std::string style;
  try {
    if (argc < 2 || std::string{argv[1]}.starts_with("--")) {
      throw std::runtime_error{"Usage: seam_phase12c_matrix output.json --plugin bundle --bank root [--development-fixture] [--style id]"};
    }
    output = std::filesystem::absolute(argv[1]).lexically_normal();
    std::unordered_set<std::string> seen;
    for (int index = 2; index < argc; ++index) {
      const std::string argument{argv[index]};
      if (!seen.insert(argument).second) throw std::runtime_error{"Duplicate argument"};
      if (argument == "--development-fixture") { fixture = true; continue; }
      if (argument != "--plugin" && argument != "--bank" && argument != "--style") throw std::runtime_error{"Unknown argument"};
      if (++index >= argc || std::string{argv[index]}.starts_with("--")) throw std::runtime_error{"Missing argument value"};
      if (argument == "--style") style = argv[index];
      else if (argument == "--plugin") plugin = std::filesystem::absolute(argv[index]).lexically_normal();
      else bank = std::filesystem::absolute(argv[index]).lexically_normal();
    }
    if (!plugin || !bank) throw std::runtime_error{"Both --plugin and --bank are required"};
    for (const auto& input : {*plugin, *bank}) {
      const auto relative = output->lexically_relative(input);
      if (*output == input || (!relative.empty() && *relative.begin() != "..")) {
        throw std::runtime_error{"Evidence output cannot overwrite the supplied plugin or bank"};
      }
    }
    outputAdmitted = true;
    const auto identity = phase12c::loadCanonicalEvidenceIdentity(*plugin, *bank, fixture, style);
    if (!identity) throw std::runtime_error{identity.error().message};
    auto rows = phase12c::runClapProcessMatrix(*plugin, *bank, identity.value(), fixture);
    std::int64_t failures{};
    bool finite = true;
    for (const auto& row : rows.asArray()) {
      if (row.asObject().at("result").asString() != "PASS") ++failures;
      finite = finite && row.asObject().at("finite").asBool();
    }
    const auto after = phase12c::loadCanonicalEvidenceIdentity(*plugin, *bank, fixture, identity.value().style);
    if (!after || after.value() != identity.value()) throw std::runtime_error{"Plugin or bank changed during execution"};
    const auto& id = identity.value();
    const Json report{Json::Object{
        {"pluginId", "com.project-seam.editor"}, {"pluginSha256", id.pluginSha256},
        {"voicebankId", id.voicebankId}, {"voicebankVersion", id.voicebankVersion},
        {"voicebankTreeSha256", id.voicebankTreeSha256}, {"voicebankContentHash", id.voicebankContentHash},
        {"style", id.style}, {"sourceCommit", id.sourceCommit}, {"buildId", id.buildId},
        {"executionPath", "clap-plugin-process-v1"},
        {"resourceMode", fixture ? "development-fixture" : "installed-bank"},
        {"evidenceScope", "engineering"}, {"releaseEligible", false},
        {"audioSampleFormat", "float32"},
        {"stateScope", "empty score with exact bank, style and output layout"},
        {"tuningProof", "rendered-sample difference; not calibrated pitch accuracy"},
        {"sourceScope", "configured-source-base-plus-exact-plugin-bytes; not clean-checkout attestation"},
        {"cases", static_cast<std::int64_t>(rows.asArray().size())}, {"expected", std::int64_t{336}},
        {"failures", failures}, {"finite", finite},
        {"result", failures == 0 ? "PASS" : "FAIL"}, {"rowResults", std::move(rows)}}};
    const auto saved = core::durableAtomicWriteText(*output, formats::stringifyJson(report, true) + "\n");
    if (!saved) throw std::runtime_error{saved.error().message};
    std::cout << "execution=clap-plugin-process-v1 cases=336 failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "CLAP matrix failed: " << error.what() << '\n';
    // An admitted failed run must not leave an older PASS at its output path.
    // Malformed command lines do not authorize writing an arbitrary path.
    if (outputAdmitted) {
      const Json failure{Json::Object{{"result", "FAIL"}, {"error", error.what()},
          {"evidenceScope", "engineering"}, {"releaseEligible", false}}};
      const auto saved = core::durableAtomicWriteText(*output, formats::stringifyJson(failure, true) + "\n");
      if (!saved) std::cerr << "Cannot record failed execution: " << saved.error().message << '\n';
    }
    return 2;
  }
}
