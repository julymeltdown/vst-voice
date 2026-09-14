// A style-owned producer has to be able to take generated material in.
//
// The style migration moved ownership of language and style onto the assignment and the take, and a
// style-owned workspace refuses a take whose style is empty. Every import path that builds a
// RawTakeInput therefore has to carry the assignment's own style, and the repository then verifies
// that the candidate's declared style agrees with it instead of accepting whatever arrived. These
// cases cover the native candidate import and the CLI collection route on a style-owned producer
// rather than the legacy producer that hides an empty style.
//
// Every identity here is a synthetic fixture: the candidate is a procedural render of one recipe,
// not a singer, and no listening or review is claimed.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/native_ui/voicebank_studio.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/voice_design/procedural_candidate.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"

#include <csignal>
#include <string>
#include <vector>

#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
#include <cerrno>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace {

using namespace seam;
namespace production = voicebank_production;

#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
int runVoicebankCli(std::vector<std::string> arguments) {
  const char* executable = SEAM_TEST_VOICEBANK_CLI;
  arguments.insert(arguments.begin(), executable);
  std::vector<char*> argv;
  for (auto& argument : arguments) argv.push_back(argument.data());
  argv.push_back(nullptr);
  pid_t process{};
  if (posix_spawn(&process, executable, nullptr, nullptr, argv.data(), environ) != 0) return -1;
  int status = 0;
  pid_t waited;
  do { waited = waitpid(process, &status, 0); } while (waited < 0 && errno == EINTR);
  if (waited != process) return -1;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
}
#endif

struct BakedCandidate final {
  std::filesystem::path recipePath, metadataPath, audioPath;
  voice_design::ProceduralCandidate candidate;
};

// One rendered candidate in one style: the recipe declares exactly the poses that style uses, so a
// candidate baked in another style cannot pass the producer's own verification.
BakedCandidate bakeCandidate(const std::filesystem::path& root, std::string_view style) {
  voice_design::VoiceRecipe recipe;
  recipe.id = "style-owned-import";
  recipe.poses = {{"a", std::string{style}, 0.0,
      {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  recipe.frications = {{"s", std::string{style}, {.seed = 42U}}};
  BakedCandidate result;
  result.recipePath = root / "recipe.json";
  CHECK(voice_design::saveVoiceRecipeFile(result.recipePath, recipe));
  // Bake from the recipe file itself, so the candidate records the identity the import will load.
  const auto resource = voice_design::loadVoiceRecipeResource(result.recipePath);
  CHECK(resource);
  if (!resource) return result;
  application::ProjectFactory factory{91100U};
  auto project = factory.createProject("Style-owned import");
  const auto track = factory.addVocalTrack(project, "Singer");
  const auto region = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{960});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960}, 69U, U"\u3055",
      domain::Language::Japanese);
  note.phoneticHint = "s a";
  project.findRegion(region)->lyrics.push_back(std::move(lyric));
  project.findRegion(region)->notes.push_back(std::move(note));
  authoring::ExportSettings settings;
  settings.includeMaster = false;
  settings.includeProceduralCandidates = true;
  const std::vector<rendering::TrackSingerSource> sources{
      rendering::TrackProceduralSource{track, resource.value(), std::string{style}}};
  CHECK(authoring::ExportService{}.exportSetWithSources(project, sources, track, region, 1U,
      root / "baked", settings));
  const auto prefix = root / "baked" / "candidates" / (track.toString() + "-" + region.toString());
  result.metadataPath = prefix.string() + ".json";
  result.audioPath = prefix.string() + ".wav";
  const auto candidate = voice_design::loadProceduralCandidate(result.metadataPath, result.audioPath,
      resource.value());
  CHECK(candidate);
  if (candidate) result.candidate = candidate.value();
  return result;
}

// A style-owned producer with one assignment that owns the given style.
struct Producer final {
  std::filesystem::path root, workspace;
  production::VoicebankProductionProject project;
};

Producer makeProducer(const std::filesystem::path& root, std::string_view style) {
  Producer result;
  result.root = root;
  result.workspace = root / "producer";
  const auto license = root / "license.txt";
  CHECK(core::durableAtomicWriteTextNew(license, "SYNTHETIC TEST ONLY: no singer qualification"));
  const auto licenseSha = core::sha256File(license);
  CHECK(licenseSha);
  result.project = {.projectId = "style-owned-import", .inventoryId = "fixture",
      .inventorySha256 = std::string(64U, 'a'), .selectedSourceStrategyId = "synthetic",
      .licenseLocator = license.string(), .licenseSha256 = licenseSha.value(),
      .immutableAssetRoot = "assets"};
  result.project.schemaVersion = production::kProductionStyleSchemaVersion;
  result.project.language = "ja";
  result.project.sourceStrategies.push_back({.id = "synthetic",
      .kind = production::SourceStrategyKind::ProceduralSynthesis,
      .rights = production::Feasibility::Pass, .coverage = production::Feasibility::NotAssessed,
      .listening = production::Feasibility::NotAssessed,
      .permissions = {.sourceUse = true, .transformation = true,
                      .singingBankRedistribution = true, .commercialRenders = true},
      .licenseLocator = license.string(), .licenseSha256 = licenseSha.value(),
      .evidenceState = "SYNTHETIC_TEST_ONLY"});
  result.project.operators = {{.operatorId = "producer", .role = "PRODUCER"}};
  result.project.unitAssignments = {{.coverageKey = "cv:s:a", .pitchLayer = 69,
      .promptId = "prompt-sa", .plannedTakeId = "take-sa", .style = std::string{style}}};
  production::ProductionProjectRepository repository{result.workspace};
  CHECK(repository.initialize(result.project, {.action = "create",
      .subjectId = result.project.projectId, .operatorId = "producer",
      .occurredAtUtc = "2026-09-14T09:00:00Z"}));
  return result;
}

}  // namespace

TEST_CASE("a style-owned producer imports a generated candidate in its assignment's own style") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("style-owned-import-native");
  const auto baked = bakeCandidate(root, "neutral");
  CHECK(baked.candidate.style == "neutral");
  const auto producer = makeProducer(root, "neutral");
  production::ProductionProjectRepository repository{producer.workspace};
  native_ui::VoicebankStudioController controller;
  CHECK(controller.openProductionProject(producer.workspace, producer.project.inventorySha256, "producer"));
  CHECK(controller.productionProject());
  CHECK(controller.selectUnit(0U));
  const auto imported = controller.importSelectedProceduralCandidate(baked.metadataPath,
      baked.audioPath, baked.recipePath, "2026-09-14T09:01:00Z");
  if (!imported) throw test::Failure{"native candidate import failed: " + imported.error().message};
  CHECK(controller.productionProject()->takes.size() == 1U);
  if (!controller.productionProject()->takes.empty()) {
    // The take carries the assignment's style, and it is still unapproved marker-review material.
    CHECK(controller.productionProject()->takes.front().takeId == "take-sa");
    CHECK(controller.productionProject()->takes.front().style == "neutral");
    CHECK(controller.productionProject()->takes.front().state == production::UnitQueueState::MarkerReview);
  }
  CHECK(controller.productionProject()->unitAssignments.front().takeId == "take-sa");
  const auto durable = repository.recover();
  CHECK(durable);
  if (durable) {
    CHECK(durable.value().takes.size() == 1U);
    if (!durable.value().takes.empty()) CHECK(durable.value().takes.front().style == "neutral");
  }
}

TEST_CASE("a candidate declared in another style is refused for a style-owned assignment") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("style-owned-import-mismatch");
  const auto baked = bakeCandidate(root, "neutral");
  const auto producer = makeProducer(root, "soft");
  production::ProductionProjectRepository repository{producer.workspace};
  native_ui::VoicebankStudioController controller;
  CHECK(controller.openProductionProject(producer.workspace, producer.project.inventorySha256, "producer"));
  CHECK(controller.productionProject());
  CHECK(controller.selectUnit(0U));
  const auto imported = controller.importSelectedProceduralCandidate(baked.metadataPath,
      baked.audioPath, baked.recipePath, "2026-09-14T09:02:00Z");
  // The candidate is refused by name rather than relabelled into the assignment's style.
  CHECK(!imported);
  CHECK(imported.error().code == core::ErrorCode::Conflict);
  const auto durable = repository.recover();
  CHECK(durable);
  if (durable) {
    CHECK(durable.value().takes.empty());
    CHECK(durable.value().unitAssignments.front().takeId.empty());
  }
}

#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
TEST_CASE("the CLI collects a generated candidate into a style-owned producer") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("style-owned-import-cli");
  const auto baked = bakeCandidate(root, "neutral");
  const auto producer = makeProducer(root, "neutral");
  production::ProductionProjectRepository repository{producer.workspace};
  const auto accepted = runVoicebankCli({"import-procedural", producer.workspace.string(),
      baked.metadataPath.string(), baked.audioPath.string(), baked.recipePath.string(),
      "take-cli", "prompt-sa", "cv:s:a", "69", "producer", "2026-09-14T09:03:00Z"});
  if (accepted != 0) throw test::Failure{"CLI collection into a style-owned producer failed"};
  const auto collected = repository.recover();
  CHECK(collected);
  if (collected) {
    CHECK(collected.value().takes.size() == 1U);
    if (!collected.value().takes.empty()) {
      CHECK(collected.value().takes.front().style == "neutral");
      CHECK(collected.value().takes.front().state == production::UnitQueueState::MarkerReview);
    }
  }
  // A take whose coverage the inventory does not declare is refused by that cause, not by the
  // style disagreement that would follow from borrowing an unrelated row.
  const auto unbound = runVoicebankCli({"import-procedural", producer.workspace.string(),
      baked.metadataPath.string(), baked.audioPath.string(), baked.recipePath.string(),
      "take-unbound", "prompt-x", "cv:x:a", "69", "producer", "2026-09-14T09:04:00Z"});
  CHECK(unbound != 0);
  const auto after = repository.recover();
  CHECK(after);
  if (after) CHECK(after.value().takes.size() == 1U);
}
#endif
