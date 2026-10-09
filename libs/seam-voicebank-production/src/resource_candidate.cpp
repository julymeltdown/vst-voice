#include "seam/voicebank_production/resource_candidate.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank/acoustic_analysis.hpp"
#include "seam/voicebank/content_identity.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank_production/project_codec.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <map>
#include <set>
#include <system_error>

namespace seam::voicebank_production {
namespace {
using J = formats::JsonValue;
constexpr std::size_t kMaximumDescriptorBytes = 32U * 1024U * 1024U;
constexpr std::size_t kMaximumListedFiles = 65536U;
constexpr std::uint64_t kMaximumListedFileBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumCandidateBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumSnapshotBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::string_view kHistoryGenerations = "provenance/history/generations/";
constexpr std::string_view kHistoryJournal = "provenance/history/journal/";

core::Result<void> malformed(std::string message, std::string context = {}) {
  return core::failure(core::ErrorCode::ParseError, std::move(message), std::move(context));
}
core::Result<void> mismatch(std::string message, std::string context = {}) {
  return core::failure(core::ErrorCode::Conflict, std::move(message), std::move(context));
}

bool isDigest(std::string_view value) {
  return value.size() == 64U && std::all_of(value.begin(), value.end(),
      [](char character) { return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'); });
}

bool safeComponent(std::string_view value) {
  return !value.empty() && value.size() <= 128U && value.front() != '.' &&
      std::all_of(value.begin(), value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '.' || character == '-' || character == '_';
      });
}

bool boundedText(std::string_view value, std::size_t maximum, bool allowEmpty = false) {
  return (allowEmpty || !value.empty()) && value.size() <= maximum &&
      std::none_of(value.begin(), value.end(), [](char character) {
        return static_cast<unsigned char>(character) < 0x20U;
      });
}

bool knownLanguage(std::string_view value) {
  return value == "ja" || value == "en" || value == "ko" || value == "und";
}

std::string languageCode(domain::Language language) {
  switch (language) {
    case domain::Language::Japanese: return "ja";
    case domain::Language::English: return "en";
    case domain::Language::Korean: return "ko";
    default: return "und";
  }
}

const std::set<std::string, std::less<>>& payloadRoles() {
  static const std::set<std::string, std::less<>> roles{
      "manifest", "sample-audio", "alignment", "acoustic-analysis", "character", "recipe", "model-graph", "model-data"};
  return roles;
}
const std::set<std::string, std::less<>>& evidenceRoles() {
  static const std::set<std::string, std::less<>> roles{
      "source-license", "source-license-snapshot", "source-quality-evidence", "production-snapshot",
      "history-generation", "history-journal", "declaration"};
  return roles;
}

bool exactKeys(const J& value, std::initializer_list<std::string_view> keys) {
  if (!value.isObject() || value.asObject().size() != keys.size()) return false;
  return std::all_of(keys.begin(), keys.end(), [&](std::string_view key) { return value.find(key) != nullptr; });
}

const std::string* text(const J& object, std::string_view key) {
  const auto* value = object.find(key);
  return value && value->isString() ? &value->asString() : nullptr;
}

std::optional<std::uint64_t> unsignedInteger(const J& object, std::string_view key) {
  const auto* value = object.find(key);
  if (!value || !value->isInteger() || value->asInt64() < 0) return std::nullopt;
  return static_cast<std::uint64_t>(value->asInt64());
}

std::optional<ResourceCandidateKind> kindFromString(std::string_view value) {
  if (value == "sample") return ResourceCandidateKind::Sample;
  if (value == "recipe") return ResourceCandidateKind::Recipe;
  if (value == "model") return ResourceCandidateKind::Model;
  return std::nullopt;
}

J encodeFile(const ResourceCandidateFile& file) {
  return J::Object{{"path", file.path}, {"role", file.role}, {"sha256", file.sha256},
      {"bytes", static_cast<std::int64_t>(file.bytes)}};
}

core::Result<void> decodeFiles(const J& root, std::string_view key, std::vector<ResourceCandidateFile>& output) {
  const auto* list = root.find(key);
  if (!list || !list->isArray() || list->asArray().size() > kMaximumListedFiles)
    return malformed("Candidate file list is missing or oversized", std::string{key});
  for (const auto& item : list->asArray()) {
    const auto* path = exactKeys(item, {"path", "role", "sha256", "bytes"}) ? text(item, "path") : nullptr;
    const auto* role = path ? text(item, "role") : nullptr;
    const auto* digest = role ? text(item, "sha256") : nullptr;
    const auto bytes = digest ? unsignedInteger(item, "bytes") : std::nullopt;
    if (!bytes) return malformed("Candidate file entry is malformed", std::string{key});
    output.push_back({*path, *role, *digest, *bytes});
  }
  return core::success();
}

// Unit review bindings and origin history keep the shapes the schema-1 builder wrote.
core::Result<void> validateUnitBindings(const ResourceCandidateDescriptor& descriptor) {
  if (descriptor.unitBindings.empty() || descriptor.unitBindings.size() > 4096U)
    return malformed("Sample candidate requires between one and 4096 unit bindings");
  std::set<std::string, std::less<>> units, takes;
  for (const auto& binding : descriptor.unitBindings) {
    if (!exactKeys(binding, {"unitId", "takeId", "audioSha256", "reviewId", "reviewMetadataRevisionId",
            "originOperatorId", "originGeneration", "originJournalSha256"}))
      return malformed("Sample candidate unit binding has unexpected fields");
    for (const auto key : {"unitId", "takeId", "reviewId", "reviewMetadataRevisionId", "originOperatorId"}) {
      const auto* value = text(binding, key);
      if (!value || !boundedText(*value, 256U)) return malformed("Sample candidate unit binding field is invalid", key);
    }
    const auto origin = unsignedInteger(binding, "originGeneration");
    if (!text(binding, "audioSha256") || !isDigest(*text(binding, "audioSha256")) ||
        !text(binding, "originJournalSha256") || !isDigest(*text(binding, "originJournalSha256")) ||
        !origin || *origin == 0U || (descriptor.source && *origin > descriptor.source->generation))
      return malformed("Sample candidate unit binding identity is invalid", *text(binding, "unitId"));
    if (!units.insert(*text(binding, "unitId")).second || !takes.insert(*text(binding, "takeId")).second)
      return malformed("Sample candidate repeats a unit or take binding", *text(binding, "unitId"));
  }
  return core::success();
}

core::Result<void> validateOriginHistory(const ResourceCandidateDescriptor& descriptor) {
  if (descriptor.originHistory.empty() || descriptor.originHistory.size() > 65536U)
    return malformed("Sample candidate requires retained origin history");
  std::uint64_t previous = 0U;
  for (const auto& entry : descriptor.originHistory) {
    const bool aborted = exactKeys(entry, {"generation", "entryType", "journalPath", "journalSha256"});
    if (!aborted && !exactKeys(entry, {"generation", "generationPath", "generationSha256", "journalPath", "journalSha256"}))
      return malformed("Sample candidate origin history entry has unexpected fields");
    const auto generation = unsignedInteger(entry, "generation");
    if (!generation || *generation <= previous || (descriptor.source && *generation > descriptor.source->generation))
      return malformed("Sample candidate origin history is not strictly ordered within its source generation");
    previous = *generation;
    if (aborted && (!text(entry, "entryType") || *text(entry, "entryType") != "aborted-write"))
      return malformed("Sample candidate origin history entry type is unknown");
    const auto* journal = text(entry, "journalPath");
    if (!journal || !journal->starts_with(kHistoryJournal) || !isPackageableCandidatePath(*journal) ||
        !text(entry, "journalSha256") || !isDigest(*text(entry, "journalSha256")))
      return malformed("Sample candidate origin journal reference is invalid");
    if (!aborted) {
      const auto* snapshot = text(entry, "generationPath");
      if (!snapshot || !snapshot->starts_with(kHistoryGenerations) || !isPackageableCandidatePath(*snapshot) ||
          !text(entry, "generationSha256") || !isDigest(*text(entry, "generationSha256")))
        return malformed("Sample candidate origin generation reference is invalid");
    }
  }
  return core::success();
}

const ResourceCandidateFile* findListed(const std::vector<ResourceCandidateFile>& files, std::string_view path) {
  const auto found = std::find_if(files.begin(), files.end(), [&](const auto& file) { return file.path == path; });
  return found == files.end() ? nullptr : &*found;
}

std::size_t countRole(const std::vector<ResourceCandidateFile>& files, std::string_view role) {
  return static_cast<std::size_t>(std::count_if(files.begin(), files.end(), [&](const auto& file) { return file.role == role; }));
}

core::Result<void> validateFileLists(const ResourceCandidateDescriptor& descriptor) {
  if (descriptor.payload.empty() || descriptor.payload.size() + descriptor.evidence.size() > kMaximumListedFiles)
    return malformed("Candidate must list between one and 65536 embedded files");
  std::set<std::string, std::less<>> paths;
  std::uint64_t total = 0U;
  for (const auto* list : {&descriptor.payload, &descriptor.evidence}) {
    const auto& roles = list == &descriptor.payload ? payloadRoles() : evidenceRoles();
    for (std::size_t index = 0U; index < list->size(); ++index) {
      const auto& file = (*list)[index];
      if (!isPackageableCandidatePath(file.path) || file.path == kResourceCandidateDescriptorPath ||
          !roles.contains(file.role) || !isDigest(file.sha256) || file.bytes > kMaximumListedFileBytes)
        return malformed("Candidate file entry is invalid for its list", file.path);
      if (index > 0U && !((*list)[index - 1U].path < file.path))
        return malformed("Candidate file lists must be strictly ordered by path", file.path);
      if (!paths.insert(file.path).second) return malformed("Candidate lists one file twice", file.path);
      total += file.bytes;
      if (total > kMaximumCandidateBytes) return malformed("Candidate exceeds its total byte budget");
    }
  }
  const auto* manifest = findListed(descriptor.payload, descriptor.rootManifest);
  if (countRole(descriptor.payload, "manifest") != 1U || !manifest || manifest->role != "manifest" ||
      manifest->sha256 != descriptor.manifestSha256)
    return malformed("Candidate root manifest must be its one listed manifest with the declared digest");
  return core::success();
}

core::Result<void> validateSample(const ResourceCandidateDescriptor& descriptor) {
  const auto& source = descriptor.source;
  if (!source || source->projectId.empty() || source->generation == 0U || !isDigest(source->projectSha256) ||
      !isDigest(source->inventorySha256) || !isDigest(source->licenseSha256))
    return malformed("Sample candidate requires its complete producer source generation");
  if (descriptor.status != kReviewedCandidateStatus || descriptor.rootManifest != "manifest.json" ||
      descriptor.languages.size() != 1U || !descriptor.externalDependencies.empty())
    return malformed("Sample candidate status, manifest, language or dependency declaration is invalid");
  for (const auto& file : descriptor.payload) {
    if (file.role == "recipe" || file.role == "model-graph" || file.role == "model-data" ||
        (file.role == "sample-audio" && (!file.path.starts_with("audio/") || !file.path.ends_with(".wav"))) ||
        (file.role == "alignment" && (!file.path.starts_with("alignments/") || !file.path.ends_with(".json"))) ||
        (file.role == "acoustic-analysis" && (!file.path.starts_with("analysis/") || !file.path.ends_with(".json"))) ||
        (file.role == "character" && !file.path.starts_with("character/")))
      return malformed("Sample candidate payload role does not fit a sample bank", file.path);
  }
  if (countRole(descriptor.payload, "sample-audio") == 0U)
    return malformed("Sample candidate lists no sample audio");
  const bool characterFiles = countRole(descriptor.payload, "character") != 0U;
  if (descriptor.character.has_value() != characterFiles ||
      (descriptor.character && !findListed(descriptor.payload, "character/manifest.json")))
    return malformed("A character reference must embed its character package and vice versa");
  const auto* license = findListed(descriptor.evidence, "source-license.txt");
  const auto* snapshot = findListed(descriptor.evidence, "provenance/production.json");
  if (!license || license->role != "source-license" || license->sha256 != source->licenseSha256 ||
      countRole(descriptor.evidence, "source-license") != 1U ||
      !snapshot || snapshot->role != "production-snapshot" || snapshot->sha256 != source->projectSha256 ||
      countRole(descriptor.evidence, "production-snapshot") != 1U)
    return malformed("Sample candidate must list its source license and exact producer snapshot as evidence");
  auto checked = validateUnitBindings(descriptor);
  if (!checked) return checked;
  checked = validateOriginHistory(descriptor);
  if (!checked) return checked;
  for (const auto& binding : descriptor.unitBindings) {
    const auto path = "audio/" + *text(binding, "audioSha256") + ".wav";
    const auto* audio = findListed(descriptor.payload, path);
    if (!audio || audio->role != "sample-audio" || audio->sha256 != *text(binding, "audioSha256"))
      return malformed("Sample candidate unit audio is not a listed dependency", path);
  }
  // A stored acoustic analysis is the record of exactly one bound unit, at the path the bank reads it from.
  std::set<std::string, std::less<>> analysisPaths;
  for (const auto& binding : descriptor.unitBindings)
    analysisPaths.insert(voicebank::acousticAnalysisSidecarPath(*text(binding, "unitId")));
  for (const auto& file : descriptor.payload)
    if (file.role == "acoustic-analysis" && !analysisPaths.contains(file.path))
      return malformed("Sample candidate lists an acoustic analysis for no bound unit", file.path);
  for (const auto& entry : descriptor.originHistory) {
    const auto* journal = findListed(descriptor.evidence, *text(entry, "journalPath"));
    if (!journal || journal->role != "history-journal" || journal->sha256 != *text(entry, "journalSha256"))
      return malformed("Sample candidate origin journal is not listed evidence", *text(entry, "journalPath"));
    if (const auto* path = text(entry, "generationPath")) {
      const auto* snapshotFile = findListed(descriptor.evidence, *path);
      if (!snapshotFile || snapshotFile->role != "history-generation" || snapshotFile->sha256 != *text(entry, "generationSha256"))
        return malformed("Sample candidate origin snapshot is not listed evidence", *path);
    }
  }
  return core::success();
}

core::Result<void> validateDeclared(const ResourceCandidateDescriptor& descriptor) {
  const bool recipe = descriptor.kind == ResourceCandidateKind::Recipe;
  if (descriptor.status != kDeclaredCandidateStatus || descriptor.source.has_value() ||
      !descriptor.unitBindings.empty() || !descriptor.originHistory.empty() || descriptor.character.has_value())
    return malformed("Recipe and model candidates carry declared facts only: no producer source, reviews or character");
  for (const auto& file : descriptor.payload) {
    const bool allowed = file.role == "manifest" ||
        (recipe ? file.role == "recipe" : (file.role == "model-graph" || file.role == "model-data"));
    if (!allowed) return malformed("Candidate payload role does not fit its resource kind", file.path);
  }
  if (std::any_of(descriptor.evidence.begin(), descriptor.evidence.end(), [](const auto& file) { return file.role != "declaration"; }))
    return malformed("Recipe and model candidates may only carry declaration evidence");
  const std::string_view dependency = recipe ? "render-engine" : "neural-runtime";
  if (std::count_if(descriptor.externalDependencies.begin(), descriptor.externalDependencies.end(),
          [&](const auto& value) { return value.kind == dependency; }) != 1)
    return malformed("Candidate must declare exactly one external runtime dependency", std::string{dependency});
  if (recipe) {
    if (countRole(descriptor.payload, "recipe") != 1U) return malformed("Recipe candidate must embed exactly one recipe");
    const auto recipeFile = std::find_if(descriptor.payload.begin(), descriptor.payload.end(),
        [](const auto& file) { return file.role == "recipe"; });
    if (recipeFile->sha256 != descriptor.contentSha256)
      return malformed("Recipe candidate content identity must be its canonical recipe digest");
  } else {
    if (countRole(descriptor.payload, "model-graph") + countRole(descriptor.payload, "model-data") == 0U)
      return malformed("Model candidate embeds no model files");
    if (modelCandidateContentSha256(descriptor.payload) != descriptor.contentSha256)
      return malformed("Model candidate content identity differs from its payload listing");
  }
  return core::success();
}

J encodeDescriptor(const ResourceCandidateDescriptor& descriptor) {
  J::Array languages, styles, payload, evidence, dependencies;
  for (const auto& value : descriptor.languages) languages.emplace_back(value);
  for (const auto& value : descriptor.styles) styles.emplace_back(value);
  for (const auto& file : descriptor.payload) payload.push_back(encodeFile(file));
  for (const auto& file : descriptor.evidence) evidence.push_back(encodeFile(file));
  for (const auto& value : descriptor.externalDependencies)
    dependencies.emplace_back(J::Object{{"kind", value.kind}, {"id", value.id}, {"revision", value.revision}});
  J character{};
  if (descriptor.character)
    character = J::Object{{"characterId", descriptor.character->characterId},
        {"characterVersion", descriptor.character->characterVersion}};
  J source{};
  if (descriptor.source)
    source = J::Object{{"projectId", descriptor.source->projectId},
        {"generation", static_cast<std::int64_t>(descriptor.source->generation)},
        {"projectSha256", descriptor.source->projectSha256}, {"inventorySha256", descriptor.source->inventorySha256},
        {"licenseSha256", descriptor.source->licenseSha256}};
  return J::Object{{"format", std::string{kResourceCandidateFormat}},
      {"schemaVersion", kResourceCandidateSchemaVersion}, {"resourceKind", std::string{toString(descriptor.kind)}},
      {"status", descriptor.status}, {"releaseEligible", false},
      {"evidenceScope", std::string{kCandidateEvidenceScope}}, {"qualification", std::string{kCandidateQualification}},
      {"resource", J::Object{{"id", descriptor.resourceId}, {"version", descriptor.resourceVersion},
          {"displayName", descriptor.displayName}}},
      {"languages", std::move(languages)}, {"styles", std::move(styles)}, {"character", std::move(character)},
      {"rootManifest", descriptor.rootManifest}, {"manifestSha256", descriptor.manifestSha256},
      {"contentSha256", descriptor.contentSha256}, {"source", std::move(source)},
      {"payload", std::move(payload)}, {"evidence", std::move(evidence)},
      {"externalDependencies", std::move(dependencies)},
      {"originHistory", descriptor.originHistory}, {"unitBindings", descriptor.unitBindings}};
}

core::Result<ResourceCandidateDescriptor> decodeLegacy(const J& root) {
  using Output = ResourceCandidateDescriptor;
  if (!exactKeys(root, {"format", "schemaVersion", "resourceKind", "status", "releaseEligible", "evidenceScope",
          "sourceProjectSha256", "sourceGeneration", "inventorySha256", "licenseSha256", "manifestSha256",
          "contentSha256", "originHistory", "unitBindings"}))
    return core::failure<Output>(core::ErrorCode::ParseError, "Schema-1 candidate descriptor has unexpected fields");
  ResourceCandidateDescriptor descriptor;
  descriptor.schemaVersion = kLegacySampleCandidateSchemaVersion;
  const auto generation = unsignedInteger(root, "sourceGeneration");
  const auto* project = text(root, "sourceProjectSha256");
  const auto* inventory = text(root, "inventorySha256");
  const auto* license = text(root, "licenseSha256");
  const auto* manifest = text(root, "manifestSha256");
  const auto* content = text(root, "contentSha256");
  const auto* history = root.find("originHistory");
  const auto* bindings = root.find("unitBindings");
  if (!text(root, "resourceKind") || *text(root, "resourceKind") != "sample" || !text(root, "status") ||
      !generation || !project || !inventory || !license || !manifest || !content ||
      !history->isArray() || !bindings->isArray())
    return core::failure<Output>(core::ErrorCode::ParseError, "Schema-1 candidate descriptor fields are malformed");
  descriptor.status = *text(root, "status");
  descriptor.source = ResourceCandidateSource{{}, *generation, *project, *inventory, *license};
  descriptor.manifestSha256 = *manifest;
  descriptor.contentSha256 = *content;
  descriptor.originHistory = history->asArray();
  descriptor.unitBindings = bindings->asArray();
  return descriptor;
}

core::Result<ResourceCandidateDescriptor> decodeTyped(const J& root) {
  using Output = ResourceCandidateDescriptor;
  if (!exactKeys(root, {"format", "schemaVersion", "resourceKind", "status", "releaseEligible", "evidenceScope",
          "qualification", "resource", "languages", "styles", "character", "rootManifest", "manifestSha256",
          "contentSha256", "source", "payload", "evidence", "externalDependencies", "originHistory", "unitBindings"}))
    return core::failure<Output>(core::ErrorCode::ParseError, "Schema-2 candidate descriptor has unexpected or missing fields");
  const auto* qualification = text(root, "qualification");
  const auto* kindName = text(root, "resourceKind");
  const auto kind = kindName ? kindFromString(*kindName) : std::nullopt;
  const auto* resource = root.find("resource");
  if (!qualification || *qualification != kCandidateQualification || !kind || !text(root, "status") ||
      !exactKeys(*resource, {"id", "version", "displayName"}) || !text(*resource, "id") ||
      !text(*resource, "version") || !text(*resource, "displayName") ||
      !text(root, "rootManifest") || !text(root, "manifestSha256") || !text(root, "contentSha256"))
    return core::failure<Output>(core::ErrorCode::ParseError, "Schema-2 candidate identity fields are malformed");
  ResourceCandidateDescriptor descriptor;
  descriptor.kind = *kind;
  descriptor.status = *text(root, "status");
  descriptor.resourceId = *text(*resource, "id");
  descriptor.resourceVersion = *text(*resource, "version");
  descriptor.displayName = *text(*resource, "displayName");
  descriptor.rootManifest = *text(root, "rootManifest");
  descriptor.manifestSha256 = *text(root, "manifestSha256");
  descriptor.contentSha256 = *text(root, "contentSha256");
  for (const auto key : {"languages", "styles"}) {
    const auto* list = root.find(key);
    if (!list->isArray() || list->asArray().size() > 64U)
      return core::failure<Output>(core::ErrorCode::ParseError, "Candidate declaration list is malformed", key);
    for (const auto& value : list->asArray()) {
      if (!value.isString()) return core::failure<Output>(core::ErrorCode::ParseError, "Candidate declaration is not text", key);
      (std::string_view{key} == "languages" ? descriptor.languages : descriptor.styles).push_back(value.asString());
    }
  }
  const auto* character = root.find("character");
  if (!character->isNull()) {
    if (!exactKeys(*character, {"characterId", "characterVersion"}) || !text(*character, "characterId") ||
        !text(*character, "characterVersion"))
      return core::failure<Output>(core::ErrorCode::ParseError, "Candidate character reference is malformed");
    descriptor.character = ResourceCandidateCharacter{*text(*character, "characterId"), *text(*character, "characterVersion")};
  }
  const auto* source = root.find("source");
  if (!source->isNull()) {
    const auto generation = unsignedInteger(*source, "generation");
    if (!exactKeys(*source, {"projectId", "generation", "projectSha256", "inventorySha256", "licenseSha256"}) ||
        !generation || !text(*source, "projectId") || !text(*source, "projectSha256") ||
        !text(*source, "inventorySha256") || !text(*source, "licenseSha256"))
      return core::failure<Output>(core::ErrorCode::ParseError, "Candidate producer source is malformed");
    descriptor.source = ResourceCandidateSource{*text(*source, "projectId"), *generation,
        *text(*source, "projectSha256"), *text(*source, "inventorySha256"), *text(*source, "licenseSha256")};
  }
  auto files = decodeFiles(root, "payload", descriptor.payload);
  if (files) files = decodeFiles(root, "evidence", descriptor.evidence);
  if (!files) return core::Result<Output>{files.error()};
  const auto* dependencies = root.find("externalDependencies");
  if (!dependencies->isArray() || dependencies->asArray().size() > 64U)
    return core::failure<Output>(core::ErrorCode::ParseError, "Candidate external dependencies are malformed");
  for (const auto& value : dependencies->asArray()) {
    if (!exactKeys(value, {"kind", "id", "revision"}) || !text(value, "kind") || !text(value, "id") || !text(value, "revision"))
      return core::failure<Output>(core::ErrorCode::ParseError, "Candidate external dependency is malformed");
    descriptor.externalDependencies.push_back({*text(value, "kind"), *text(value, "id"), *text(value, "revision")});
  }
  const auto* history = root.find("originHistory");
  const auto* bindings = root.find("unitBindings");
  if (!history->isArray() || !bindings->isArray())
    return core::failure<Output>(core::ErrorCode::ParseError, "Candidate provenance lists are malformed");
  descriptor.originHistory = history->asArray();
  descriptor.unitBindings = bindings->asArray();
  return descriptor;
}

core::Result<std::string> hashListedFile(const std::filesystem::path& path, std::uint64_t expectedBytes) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size != expectedBytes)
    return core::failure<std::string>(core::ErrorCode::Conflict, "Candidate file size differs from its listing", path.string());
  return core::sha256File(path, std::max<std::uint64_t>(expectedBytes, 1U));
}

core::Result<void> verifySampleFiles(const std::filesystem::path& root, const ResourceCandidateDescriptor& descriptor,
                                     std::stop_token stop) {
  const auto manifestText = core::readTextFileLimited(root / "manifest.json", kMaximumDescriptorBytes);
  if (!manifestText) return core::Result<void>{manifestText.error()};
  if (core::sha256Hex(manifestText.value()) != descriptor.manifestSha256)
    return mismatch("Candidate manifest bytes differ from the declared digest");
  const auto manifest = voicebank::ManifestJsonCodec{}.decode(manifestText.value());
  if (!manifest) return core::Result<void>{manifest.error()};
  if (stop.stop_requested()) return mismatch("Candidate verification cancelled");
  const auto content = voicebank::computeVoicebankContentHash(manifest.value(), root);
  if (!content) return core::Result<void>{content.error()};
  if (content.value() != descriptor.contentSha256) return mismatch("Candidate voicebank content differs from its identity");
  const auto& source = *descriptor.source;
  const auto snapshotText = core::readTextFileLimited(root / "provenance/production.json", kMaximumSnapshotBytes);
  if (!snapshotText) return core::Result<void>{snapshotText.error()};
  const auto snapshot = decodeProductionProject(snapshotText.value());
  if (core::sha256Hex(snapshotText.value()) != source.projectSha256 || !snapshot ||
      snapshot.value().lastDurableGeneration != source.generation ||
      snapshot.value().inventorySha256 != source.inventorySha256 ||
      (descriptor.declaresDependencySet() && snapshot.value().projectId != source.projectId))
    return mismatch("Candidate producer snapshot does not reopen as its declared source generation");
  const auto license = core::sha256File(root / "source-license.txt", 4ULL * 1024ULL * 1024ULL);
  if (!license || license.value() != source.licenseSha256) return mismatch("Candidate source license differs from its identity");
  if (descriptor.unitBindings.size() != manifest.value().units.size())
    return mismatch("Candidate unit bindings do not cover the manifest exactly");
  for (const auto& binding : descriptor.unitBindings) {
    const auto* unit = manifest.value().findUnit(*text(binding, "unitId"));
    const auto expected = "audio/" + *text(binding, "audioSha256") + ".wav";
    if (!unit || unit->audioPath.generic_string() != expected)
      return mismatch("Candidate unit binding does not name its manifest unit audio", *text(binding, "unitId"));
    if (!descriptor.declaresDependencySet()) {
      const auto audio = core::sha256File(root / expected, kMaximumListedFileBytes);
      if (!audio || audio.value() != *text(binding, "audioSha256")) return mismatch("Candidate unit audio differs", expected);
    }
  }
  if (!descriptor.declaresDependencySet()) {
    for (const auto& entry : descriptor.originHistory) {
      for (const auto& [pathKey, digestKey] : {std::pair{"journalPath", "journalSha256"}, std::pair{"generationPath", "generationSha256"}}) {
        const auto* path = text(entry, pathKey);
        if (!path) continue;
        const auto digest = core::sha256File(root / *path, kMaximumSnapshotBytes);
        if (!digest || digest.value() != *text(entry, digestKey)) return mismatch("Candidate retained history differs", *path);
      }
    }
    return core::success();
  }
  const auto code = languageCode(manifest.value().language);
  const std::optional<ResourceCandidateCharacter> character = manifest.value().characterId.empty()
      ? std::nullopt : std::optional{ResourceCandidateCharacter{manifest.value().characterId, manifest.value().characterVersion}};
  if (manifest.value().id != descriptor.resourceId || manifest.value().version != descriptor.resourceVersion ||
      manifest.value().displayName != descriptor.displayName || descriptor.languages.front() != code ||
      manifest.value().styles != descriptor.styles || character != descriptor.character)
    return mismatch("Candidate declared identity, language, styles or character differ from its manifest");
  std::set<std::string, std::less<>> referenced;
  for (const auto& unit : manifest.value().units) referenced.insert(unit.audioPath.generic_string());
  for (const auto& file : descriptor.payload) {
    if (file.role == "sample-audio" && !referenced.contains(file.path))
      return mismatch("Candidate lists sample audio its manifest does not use", file.path);
  }
  for (const auto& path : referenced) {
    const auto* listed = findListed(descriptor.payload, path);
    if (!listed || listed->role != "sample-audio") return mismatch("Candidate manifest uses unlisted audio", path);
  }
  return core::success();
}

}  // namespace

std::string_view toString(ResourceCandidateKind kind) noexcept {
  switch (kind) {
    case ResourceCandidateKind::Sample: return "sample";
    case ResourceCandidateKind::Recipe: return "recipe";
    case ResourceCandidateKind::Model: return "model";
  }
  return "sample";
}

bool isPackageableCandidatePath(std::string_view path) noexcept {
  // Mirrors the signed-container path and asset rules so no listed file can fail packaging later.
  if (path.empty() || path.size() > 1024U || path.front() == '/' || path.back() == '/' ||
      path.find('\\') != std::string_view::npos || path.find(':') != std::string_view::npos ||
      path.find('\0') != std::string_view::npos) return false;
  std::size_t start = 0U;
  while (start < path.size()) {
    const auto slash = path.find('/', start);
    const auto segment = path.substr(start, slash == std::string_view::npos ? path.size() - start : slash - start);
    if (segment.empty() || segment == "." || segment == ".." || segment.front() == '.') return false;
    start = slash == std::string_view::npos ? path.size() : slash + 1U;
  }
  std::string lower{path};
  std::transform(lower.begin(), lower.end(), lower.begin(),
      [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
  const auto filename = std::filesystem::path{lower}.filename().string();
  if (filename == "license" || filename == "notice") return true;
  static const std::set<std::string, std::less<>> allowed{
      ".json", ".cbor", ".wav", ".bin", ".dat", ".png", ".webp", ".ppm", ".pgm", ".txt", ".md", ".license"};
  return allowed.contains(std::filesystem::path{lower}.extension().string());
}

std::string modelCandidateContentSha256(const std::vector<ResourceCandidateFile>& payload) {
  std::string canonical{"seam-model-candidate-content-v1\n"};
  for (const auto& file : payload)
    canonical += file.path + '\t' + file.role + '\t' + file.sha256 + '\t' + std::to_string(file.bytes) + '\n';
  return core::sha256Hex(canonical);
}

core::Result<void> validateResourceCandidateDescriptor(const ResourceCandidateDescriptor& descriptor) {
  if (descriptor.schemaVersion == kLegacySampleCandidateSchemaVersion) {
    if (descriptor.kind != ResourceCandidateKind::Sample || descriptor.status != kReviewedCandidateStatus ||
        !descriptor.source || descriptor.source->generation == 0U || !isDigest(descriptor.source->projectSha256) ||
        !isDigest(descriptor.source->inventorySha256) || !isDigest(descriptor.source->licenseSha256) ||
        !isDigest(descriptor.manifestSha256) || !isDigest(descriptor.contentSha256) ||
        !descriptor.payload.empty() || !descriptor.evidence.empty() || !descriptor.languages.empty())
      return malformed("Schema-1 sample candidate identity is invalid");
    auto checked = validateUnitBindings(descriptor);
    return checked ? validateOriginHistory(descriptor) : checked;
  }
  if (descriptor.schemaVersion != kResourceCandidateSchemaVersion)
    return malformed("Unsupported resource candidate schema version");
  if (!safeComponent(descriptor.resourceId) || !safeComponent(descriptor.resourceVersion) ||
      !boundedText(descriptor.displayName, 256U, true) || !isDigest(descriptor.manifestSha256) ||
      !isDigest(descriptor.contentSha256) || !isPackageableCandidatePath(descriptor.rootManifest) ||
      descriptor.rootManifest == kResourceCandidateDescriptorPath)
    return malformed("Candidate resource identity is invalid");
  std::set<std::string, std::less<>> seen;
  if (descriptor.languages.empty() || descriptor.styles.empty())
    return malformed("Candidate must declare its applicable languages and styles");
  for (const auto& language : descriptor.languages)
    if (!knownLanguage(language) || !seen.insert("language:" + language).second)
      return malformed("Candidate language declaration is unknown or repeated", language);
  for (const auto& style : descriptor.styles)
    if (!boundedText(style, 64U) || !seen.insert("style:" + style).second)
      return malformed("Candidate style declaration is invalid or repeated", style);
  if (descriptor.character && (!boundedText(descriptor.character->characterId, 128U) ||
                               !boundedText(descriptor.character->characterVersion, 128U)))
    return malformed("Candidate character reference is incomplete");
  for (const auto& dependency : descriptor.externalDependencies)
    if (!boundedText(dependency.kind, 64U) || !boundedText(dependency.id, 128U) || !boundedText(dependency.revision, 64U) ||
        !seen.insert("dependency:" + dependency.kind + "/" + dependency.id).second)
      return malformed("Candidate external dependency is invalid or repeated", dependency.kind);
  auto checked = validateFileLists(descriptor);
  if (!checked) return checked;
  return descriptor.kind == ResourceCandidateKind::Sample ? validateSample(descriptor) : validateDeclared(descriptor);
}

core::Result<std::string> encodeResourceCandidateDescriptor(const ResourceCandidateDescriptor& descriptor) {
  if (descriptor.schemaVersion != kResourceCandidateSchemaVersion)
    return core::failure<std::string>(core::ErrorCode::Unsupported,
        "Only schema-2 candidates are written; legacy descriptors are never re-encoded");
  const auto valid = validateResourceCandidateDescriptor(descriptor);
  if (!valid) return core::Result<std::string>{valid.error()};
  auto encoded = formats::stringifyJson(encodeDescriptor(descriptor), true) + "\n";
  if (encoded.size() > kMaximumDescriptorBytes)
    return core::failure<std::string>(core::ErrorCode::Unsupported, "Candidate descriptor exceeds its byte limit");
  const auto reopened = decodeResourceCandidateDescriptor(encoded);
  if (!reopened) return core::Result<std::string>{reopened.error()};
  return encoded;
}

core::Result<ResourceCandidateDescriptor> decodeResourceCandidateDescriptor(std::string_view json) {
  using Output = ResourceCandidateDescriptor;
  const auto parsed = formats::parseJson(json, formats::JsonParseLimits{.maximumInputBytes = kMaximumDescriptorBytes,
      .maximumDepth = 16U, .maximumNodes = 1'000'000U, .maximumStringBytes = 64U * 1024U,
      .maximumCollectionEntries = 250'000U});
  if (!parsed) return core::Result<Output>{parsed.error()};
  const auto& root = parsed.value();
  const auto* format = root.isObject() ? text(root, "format") : nullptr;
  const auto* schema = format ? root.find("schemaVersion") : nullptr;
  const auto* eligible = root.isObject() ? root.find("releaseEligible") : nullptr;
  const auto* scope = root.isObject() ? text(root, "evidenceScope") : nullptr;
  if (!format || *format != kResourceCandidateFormat || !schema || !schema->isInteger())
    return core::failure<Output>(core::ErrorCode::ParseError, "Not a resource candidate descriptor");
  // A candidate is never release eligible and never leaves engineering evidence scope here.
  if (!eligible || !eligible->isBool() || eligible->asBool() || !scope || *scope != kCandidateEvidenceScope)
    return core::failure<Output>(core::ErrorCode::Conflict, "Candidate descriptor claims release eligibility or evidence scope");
  auto decoded = schema->asInt64() == kLegacySampleCandidateSchemaVersion ? decodeLegacy(root)
      : schema->asInt64() == kResourceCandidateSchemaVersion ? decodeTyped(root)
      : core::failure<Output>(core::ErrorCode::Unsupported, "Unsupported resource candidate schema version");
  if (!decoded) return decoded;
  const auto valid = validateResourceCandidateDescriptor(decoded.value());
  if (!valid) return core::Result<Output>{valid.error()};
  return decoded;
}

core::Result<VerifiedResourceCandidate> verifyResourceCandidateDirectory(const std::filesystem::path& root,
                                                                        std::stop_token stop) {
  using Output = VerifiedResourceCandidate;
  std::error_code error;
  const auto rootStatus = std::filesystem::symlink_status(root, error);
  if (error || !std::filesystem::is_directory(rootStatus) || std::filesystem::is_symlink(rootStatus))
    return core::failure<Output>(core::ErrorCode::NotFound, "Candidate directory must be a real directory", root.string());
  const auto descriptorPath = root / std::string{kResourceCandidateDescriptorPath};
  const auto descriptorStatus = std::filesystem::symlink_status(descriptorPath, error);
  if (error || !std::filesystem::is_regular_file(descriptorStatus) || std::filesystem::is_symlink(descriptorStatus))
    return core::failure<Output>(core::ErrorCode::NotFound, "Candidate descriptor must be a regular file", descriptorPath.string());
  const auto descriptorText = core::readTextFileLimited(descriptorPath, kMaximumDescriptorBytes);
  if (!descriptorText) return core::Result<Output>{descriptorText.error()};
  auto descriptor = decodeResourceCandidateDescriptor(descriptorText.value());
  if (!descriptor) return core::Result<Output>{descriptor.error()};
  std::map<std::string, std::uint64_t, std::less<>> present;
  for (std::filesystem::recursive_directory_iterator iterator{root, error}, end; !error && iterator != end;
       iterator.increment(error)) {
    if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Candidate verification cancelled");
    const auto status = iterator->symlink_status(error);
    if (error) break;
    const auto relative = iterator->path().lexically_relative(root).generic_string();
    if (std::filesystem::is_symlink(status) || (!std::filesystem::is_directory(status) && !std::filesystem::is_regular_file(status)))
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate contains a link or special file", relative);
    if (std::filesystem::is_directory(status)) continue;
    if (present.size() >= kMaximumListedFiles + 1U)
      return core::failure<Output>(core::ErrorCode::Unsupported, "Candidate contains too many files");
    const auto size = iterator->file_size(error);
    if (error) return core::failure<Output>(core::ErrorCode::IoError, "Cannot size candidate file", relative);
    present.emplace(relative, static_cast<std::uint64_t>(size));
  }
  if (error) return core::failure<Output>(core::ErrorCode::IoError, "Cannot enumerate candidate directory", error.message());
  const auto& value = descriptor.value();
  if (value.declaresDependencySet()) {
    if (present.size() != value.payload.size() + value.evidence.size() + 1U)
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate directory holds unlisted or missing files");
    for (const auto* list : {&value.payload, &value.evidence}) {
      for (const auto& file : *list) {
        if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Candidate verification cancelled");
        const auto found = present.find(file.path);
        if (found == present.end() || found->second != file.bytes)
          return core::failure<Output>(core::ErrorCode::Conflict, "Candidate file is missing or differs in size", file.path);
        const auto digest = hashListedFile(root / file.path, file.bytes);
        if (!digest || digest.value() != file.sha256)
          return core::failure<Output>(core::ErrorCode::Conflict, "Candidate file differs from its listed digest", file.path);
      }
    }
  }
  if (value.kind == ResourceCandidateKind::Sample) {
    const auto checked = verifySampleFiles(root, value, stop);
    if (!checked) return core::Result<Output>{checked.error()};
  } else {
    const auto manifest = core::sha256File(root / value.rootManifest, kMaximumDescriptorBytes);
    if (!manifest || manifest.value() != value.manifestSha256)
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate root manifest differs from its identity");
  }
  const auto candidateSha256 = core::sha256Hex(descriptorText.value());
  const auto reread = core::sha256File(descriptorPath, kMaximumDescriptorBytes);
  if (!reread || reread.value() != candidateSha256)
    return core::failure<Output>(core::ErrorCode::Conflict, "Candidate descriptor changed during verification");
  return VerifiedResourceCandidate{root, std::move(descriptor.value()), candidateSha256};
}

}  // namespace seam::voicebank_production
