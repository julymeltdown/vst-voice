#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/file_io.hpp"
#include "seam/native_ui/voicebank_studio.hpp"
#include "seam/voicebank_production/draft_inventory.hpp"
#include "seam/voicebank_production/repository.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace {
namespace core = seam::core;
namespace production = seam::voicebank_production;
using Controller = seam::native_ui::VoicebankStudioController;

core::Result<void> drain(Controller& controller) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{120};
  while (controller.proceduralImportBusy()) {
    auto result = controller.pollProceduralCandidateImport();
    if (!result) return result;
    if (std::chrono::steady_clock::now() >= deadline)
      throw seam::test::Failure{"Studio workspace worker did not finish"};
    if (controller.proceduralImportBusy()) std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return core::success();
}

std::vector<std::string> entries(const std::filesystem::path& directory) {
  std::vector<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator{directory})
    names.push_back(entry.path().filename().string());
  std::sort(names.begin(), names.end());
  return names;
}
}  // namespace

TEST_CASE("Studio creates a producer folder, opens it and reopens it without a typed digest") {
  const auto parent = seam::test::support::temporaryDirectory("studio-producer-create");
  const auto destination = parent / "first-voice";
  Controller creator;
  CHECK(creator.beginCreateProductionProject(destination, "first-voice", "producer", "2026-09-29T01:00:00Z"));
  CHECK(creator.proceduralImportBusy());
  CHECK(creator.workspaceOpening());
  // A second entry cannot start while the first is running.
  CHECK(!creator.beginOpenProducerFolder(destination, "producer"));
  CHECK(drain(creator));
  const auto* project = creator.productionProject();
  CHECK(project != nullptr);
  if (project == nullptr) return;
  CHECK(project->unitAssignments.size() == 1026U);
  CHECK(project->takes.empty());
  CHECK(creator.status().find("PRODUCER WORKSPACE CREATED") != std::string_view::npos);
  CHECK(creator.status().find("RANGE NOT ASSESSED") != std::string_view::npos);
  const auto& created = creator.createdProducerWorkspace();
  CHECK(created.has_value());
  if (!created) return;
  CHECK(created->root == std::filesystem::canonical(destination));
  CHECK(project->inventorySha256 == created->inventorySha256);
  // Entering a workspace cannot replace the one already open.
  CHECK(!creator.beginCreateProductionProject(parent / "second-voice", "second-voice", "producer",
      "2026-09-29T01:00:01Z"));
  CHECK(entries(parent) == std::vector<std::string>{"first-voice"});

  Controller reopened;
  CHECK(reopened.beginOpenProducerFolder(destination, "producer"));
  CHECK(drain(reopened));
  CHECK(reopened.productionProject() != nullptr);
  if (reopened.productionProject() != nullptr) {
    CHECK(reopened.productionProject()->inventorySha256 == created->inventorySha256);
    CHECK(reopened.productionProject()->lastDurableGeneration == project->lastDurableGeneration);
  }
  CHECK(!reopened.createdProducerWorkspace().has_value());
  CHECK(reopened.status() == "PRODUCTION RECOVERED");

  // An unregistered operator is refused and Studio stays source-free.
  Controller stranger;
  CHECK(stranger.beginOpenProducerFolder(destination, "someone-else"));
  CHECK(!drain(stranger));
  CHECK(stranger.productionProject() == nullptr);

  // An edited inventory no longer matches its profile and is refused before opening.
  const auto inventoryPath = destination / "inventory.json";
  const auto original = core::readTextFileLimited(inventoryPath, 64U * 1024U * 1024U);
  CHECK(original);
  if (!original) return;
  auto edited = original.value();
  const auto at = edited.find("\"alternateTakes\": 2");
  CHECK(at != std::string::npos);
  if (at == std::string::npos) return;
  edited.replace(at, 19U, "\"alternateTakes\": 1");
  CHECK(core::durableAtomicWriteText(inventoryPath, edited));
  Controller tampered;
  CHECK(tampered.beginOpenProducerFolder(destination, "producer"));
  CHECK(!drain(tampered));
  CHECK(tampered.productionProject() == nullptr);
}

TEST_CASE("Studio creates a vowel starter workspace whose ten rows reopen from its folder") {
  const auto parent = seam::test::support::temporaryDirectory("studio-producer-starter");
  const auto destination = parent / "starter-voice";
  Controller creator;
  CHECK(creator.beginCreateProductionProject(destination, "starter-voice", "producer", "2026-09-29T01:00:00Z",
      production::DraftInventoryPreset::JapaneseVowelStarter));
  CHECK(drain(creator));
  const auto* project = creator.productionProject();
  CHECK(project != nullptr);
  if (project == nullptr) return;
  CHECK(project->unitAssignments.size() == 10U);
  CHECK(creator.selectableUnitCount() == 10U);
  CHECK(creator.status().find("PRODUCER WORKSPACE CREATED / 10 UNITS MISSING") != std::string_view::npos);
  Controller reopened;
  CHECK(reopened.beginOpenProducerFolder(destination, "producer"));
  CHECK(drain(reopened));
  CHECK(reopened.productionProject() != nullptr);
  if (reopened.productionProject() != nullptr) CHECK(reopened.productionProject()->unitAssignments.size() == 10U);
}

TEST_CASE("Studio workspace creation refusals and cancellation never leave a half-made folder") {
  const auto parent = seam::test::support::temporaryDirectory("studio-producer-refusal");
  Controller refused;
  CHECK(!refused.beginCreateProductionProject({}, "voice", "producer", "2026-09-29T01:00:00Z"));
  CHECK(!refused.beginCreateProductionProject(parent / "voice", "", "producer", "2026-09-29T01:00:00Z"));
  CHECK(!refused.beginCreateProductionProject(parent / "voice", "voice", "", "2026-09-29T01:00:00Z"));
  CHECK(refused.beginCreateProductionProject(std::filesystem::path{"relative"}, "voice", "producer",
      "2026-09-29T01:00:00Z"));
  CHECK(!drain(refused));
  CHECK(refused.productionProject() == nullptr);
  CHECK(!refused.createdProducerWorkspace().has_value());

  std::filesystem::create_directory(parent / "occupied");
  Controller occupied;
  CHECK(occupied.beginCreateProductionProject(parent / "occupied", "voice", "producer", "2026-09-29T01:00:00Z"));
  const auto occupiedResult = drain(occupied);
  CHECK(!occupiedResult);
  if (!occupiedResult) CHECK(occupiedResult.error().message.find("already exists") != std::string::npos);
  CHECK(occupied.productionProject() == nullptr);
  CHECK(entries(parent / "occupied").empty());

  // Cancellation may win before publication (nothing is created) or arrive after
  // it (the published folder is the one Studio opens). Both leave one truth.
  Controller cancelled;
  CHECK(cancelled.beginCreateProductionProject(parent / "cancelled", "voice", "producer", "2026-09-29T01:00:00Z"));
  cancelled.cancelProceduralCandidateImport();
  CHECK(drain(cancelled));
  if (cancelled.productionProject() == nullptr) {
    CHECK(cancelled.status() == "WORKSPACE CREATION CANCELLED / NOTHING CREATED");
    CHECK(!std::filesystem::exists(parent / "cancelled"));
  } else {
    CHECK(cancelled.createdProducerWorkspace().has_value());
    CHECK(std::filesystem::exists(parent / "cancelled" / "producer"));
  }
  const auto names = entries(parent);
  CHECK(std::none_of(names.begin(), names.end(), [](const auto& name) { return name.starts_with("."); }));
}
