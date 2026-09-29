#pragma once

#include "seam/core/result.hpp"
#include "seam/voicebank_production/project.hpp"

#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace seam::voicebank_production {

// Style-owned (schema 2) recording inventories, generated exactly as
// tools/voicebank_script_generator/draft_inventory.py generates them. The
// document bytes and both digests match that generator, so the external
// validator admits a workspace created here. Generation is not range
// qualification: the requested range is always recorded as NOT_ASSESSED.
inline constexpr std::string_view kDraftInventoryGeneratorVersion{"seam-draft-inventory-2.0.0"};
inline constexpr std::size_t kDraftInventoryMaximumUnits{16384U};

// Field defaults equal the Python draft profile defaults; only profileId has none.
struct DraftInventoryProfile final {
  std::string profileId;
  std::string language{"ja"};
  std::vector<std::string> supportedStyles{"neutral"};
  std::vector<std::string> vowels{"a", "i", "u", "e", "o"};
  std::vector<std::string> consonants{
      "k", "g", "s", "sh", "z", "j", "t", "ch", "ts", "d", "n", "h", "f", "b", "p",
      "m", "y", "r", "w", "v", "ky", "gy", "ny", "hy", "by", "py", "my", "ry", "fy", "vy"};
  std::vector<std::string> specialPhones{"N", "R", "pau", "br", "cl", "glottal"};
  std::vector<std::string> includeKinds{
      "sustain", "release", "breath", "glottal-attack", "special", "cv", "vc", "vv"};
  std::vector<std::int64_t> pitchLayers{60, 66, 72};
  std::int64_t requestedMinMidi{60};
  std::int64_t requestedMaxMidi{72};
  std::int64_t alternateTakes{2};
  std::int64_t sessionBlockSize{24};
};

struct DraftInventoryUnit final {
  std::string promptId, takeId, style, kind, coverageKey, assignmentId, retakeGroup, filename;
  std::vector<std::string> phones;
  std::int64_t pitchLayer{0};
  std::int64_t sessionBlock{0};
};

struct DraftInventory final {
  DraftInventoryProfile profile;
  std::vector<std::string> requiredCoverage;
  std::vector<DraftInventoryUnit> units;
  std::string inventorySha256;
  std::string scriptSha256;
  // The complete document with both digests, pretty printed. Write these bytes.
  std::string documentJson;
  // The operator recording script; scriptSha256 covers exactly these bytes.
  std::string operatorCsv;
};

// Reads a profile object: missing fields take the defaults and unknown fields are
// refused (a range test or PASS result is not draft authority).
[[nodiscard]] core::Result<DraftInventoryProfile> parseDraftInventoryProfile(std::string_view json);
// Checks every generator bound before producing anything.
[[nodiscard]] core::Result<DraftInventory> generateDraftInventory(const DraftInventoryProfile& profile);
// Admits a document only when its own profile regenerates it exactly, which
// covers every unit, path, assessment and both digests.
[[nodiscard]] core::Result<DraftInventory> loadDraftInventory(std::string_view documentJson);
// The empty schema-4 Draft producer for this inventory: every assignment Missing,
// one registered PRODUCER, and no source, review or feasibility claims.
[[nodiscard]] core::Result<VoicebankProductionProject> makeDraftProducerProject(
    const DraftInventory& inventory, std::string projectId, std::string producerId);

// A producer folder Studio can create and reopen without hand-written JSON:
//   DESTINATION/inventory.json         the generated inventory document
//   DESTINATION/recording-script.csv   the operator script it describes
//   DESTINATION/producer/              the initialized production workspace
inline constexpr std::string_view kDraftProducerInventoryFile{"inventory.json"};
inline constexpr std::string_view kDraftProducerScriptFile{"recording-script.csv"};
inline constexpr std::string_view kDraftProducerWorkspaceDirectory{"producer"};

struct CreatedDraftProducerWorkspace final {
  std::filesystem::path root, producerRoot, inventoryPath, scriptPath;
  std::string projectId, producerId, inventorySha256, scriptSha256, projectSha256;
  std::uint64_t generation{0U};
  std::size_t units{0U}, assignments{0U};
  bool durabilityConfirmed{true};
  std::vector<std::string> diagnostics;
};

// Creates DESTINATION as a new directory in one no-overwrite publication. The
// destination must be absolute and absent; a failure leaves nothing behind.
[[nodiscard]] core::Result<CreatedDraftProducerWorkspace> createDraftProducerWorkspace(
    const std::filesystem::path& destination, const DraftInventoryProfile& profile,
    std::string projectId, std::string producerId, std::string occurredAtUtc,
    std::stop_token stop = {});

struct DraftProducerWorkspaceInventory final {
  std::filesystem::path root, producerRoot;
  DraftInventory inventory;
};
// Reads and verifies ROOT/inventory.json so ROOT/producer can be opened against
// its inventory digest without the operator typing it.
[[nodiscard]] core::Result<DraftProducerWorkspaceInventory> readDraftProducerWorkspaceInventory(
    const std::filesystem::path& root);

}  // namespace seam::voicebank_production
