#include "seam/authoring/inventory_generation.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include <algorithm>

namespace seam::authoring {
core::Result<InventoryGenerationScore> buildInventoryGenerationScore(
    const voicebank_production::VoicebankProductionProject& producer, std::string_view plannedTakeId) {
  const auto invalid = [](std::string message) {
    return core::failure<InventoryGenerationScore>(core::ErrorCode::InvalidArgument, std::move(message));
  };
  if (producer.schemaVersion != voicebank_production::kProductionStyleSchemaVersion || producer.language != "ja")
    return invalid("Inventory score template requires a style-owned Japanese workspace");
  const auto matches = [&](const auto& row) { return row.plannedTakeId == plannedTakeId; };
  if (plannedTakeId.empty() || std::count_if(producer.unitAssignments.begin(), producer.unitAssignments.end(), matches) != 1)
    return invalid("Inventory take must identify exactly one assignment");
  const auto& row = *std::find_if(producer.unitAssignments.begin(), producer.unitAssignments.end(), matches);
  if (row.style.empty() || row.style.size() > 128U || row.pitchLayer < 24 || row.pitchLayer > 96 || row.coverageKey.size() > 256U)
    return invalid("Inventory assignment style, pitch or coverage is invalid");
  const auto separator = row.coverageKey.find(':');
  if (separator == std::string::npos) return invalid("Inventory coverage must retain kind and phones");
  const auto kind = row.coverageKey.substr(0, separator);
  const std::vector<std::string> kinds{"sustain", "release", "breath", "glottal-attack", "special", "cv", "vc", "vv"};
  if (std::find(kinds.begin(), kinds.end(), kind) == kinds.end()) return invalid("Unknown inventory coverage kind");
  std::string hint = row.coverageKey.substr(separator + 1U);
  std::replace(hint.begin(), hint.end(), ':', ' ');
  const auto phones = phonemizer::parseJapanesePhoneHint(hint);
  if (!phones) return invalid("Inventory phone sequence is not supported by the Japanese score adapter: " + phones.error().message);
  const auto expectedCount = (kind == "sustain" || kind == "breath" || kind == "special") ? 1U : 2U;
  if (phones.value().size() != expectedCount) return invalid("Inventory kind and phone count disagree");
  std::string canonical = kind;
  for (const auto& phone : phones.value()) canonical += ":" + phone;
  if (canonical != row.coverageKey) return invalid("Inventory phone sequence is not canonical");
  application::ProjectFactory factory{1U};
  auto project = factory.createProject("Inventory generation template v1");
  const auto track = factory.addVocalTrack(project, "Inventory " + row.style);
  const auto region = factory.addRegion(project, track, row.coverageKey, time::Tick{0}, time::Tick{960});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960},
      static_cast<std::uint8_t>(row.pitchLayer), U"inventory", domain::Language::Japanese);
  note.phoneticHint = hint;
  project.findRegion(region)->lyrics.push_back(std::move(lyric));
  project.findRegion(region)->notes.push_back(std::move(note));
  const auto encoded = formats::ProjectJsonCodec{}.encode(project);
  if (!encoded) return core::Result<InventoryGenerationScore>{encoded.error()};
  return InventoryGenerationScore{std::move(project), track, region, "inventory-score-v1-" + core::sha256Hex(encoded.value())};
}

core::Result<PreparedGenerationJob> prepareInventoryGenerationJob(
    const std::filesystem::path& scorePath, const std::filesystem::path& jobDirectory,
    const voicebank_production::VoicebankProductionProject& producer, std::string_view plannedTakeId,
    GenerationRecipeSelection recipe, std::stop_token stop) {
  if (stop.stop_requested()) return core::failure<PreparedGenerationJob>(core::ErrorCode::Conflict, "Inventory generation cancelled");
  auto score = buildInventoryGenerationScore(producer, plannedTakeId);
  if (!score) return core::Result<PreparedGenerationJob>{score.error()};
  const auto& row = *std::find_if(producer.unitAssignments.begin(), producer.unitAssignments.end(),
      [&](const auto& value) { return value.plannedTakeId == plannedTakeId; });
  if (recipe.style != row.style) return core::failure<PreparedGenerationJob>(core::ErrorCode::Conflict, "Recipe style differs from inventory ownership");
  const auto encoded = formats::ProjectJsonCodec{}.encode(score.value().project);
  if (!encoded) return core::Result<PreparedGenerationJob>{encoded.error()};
  const auto saved = core::durableAtomicWriteTextNew(scorePath, encoded.value());
  if (!saved) return core::Result<PreparedGenerationJob>{saved.error()};
  const auto jobId = "inventory-v1-" + core::sha256Hex(score.value().templateIdentity + "\n" + std::string{plannedTakeId});
  return prepareGenerationJobFromScore(jobDirectory, jobId, scorePath,
      score.value().trackId, score.value().regionId, producer, plannedTakeId, stop,
      core::sha256Hex(encoded.value()), std::move(recipe));
}
}
