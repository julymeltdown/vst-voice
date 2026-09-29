#include "seam/voicebank_production/draft_inventory.hpp"

#include "candidate_publication_internal.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <random>
#include <set>
#include <utility>

namespace seam::voicebank_production {
namespace {
using J = formats::JsonValue;
namespace publication = candidate_publication_internal;

constexpr std::array<std::string_view, 11U> kProfileFields{
    "profileId", "language", "supportedStyles", "vowels", "consonants", "specialPhones",
    "includeKinds", "pitchLayers", "requestedRange", "alternateTakes", "sessionBlockSize"};
constexpr std::array<std::string_view, 8U> kKinds{
    "sustain", "release", "breath", "glottal-attack", "special", "cv", "vc", "vv"};
constexpr std::array<std::string_view, 11U> kCsvFields{
    "promptId", "takeId", "language", "style", "kind", "phones", "pronunciationHint",
    "pitchLayer", "sessionBlock", "retakeGroup", "filename"};
constexpr std::uint64_t kMaximumInventoryBytes{64ULL * 1024ULL * 1024ULL};

template <class T>
core::Result<T> invalid(std::string message) {
  return core::failure<T>(core::ErrorCode::InvalidArgument, std::move(message));
}

// Strict UTF-8: no overlong forms, surrogates or values past U+10FFFF.
bool validUtf8(std::string_view text) {
  std::size_t index = 0U;
  while (index < text.size()) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t length = 0U;
    std::uint32_t value = 0U;
    if (lead < 0x80U) { ++index; continue; }
    if (lead >= 0xC2U && lead <= 0xDFU) { length = 2U; value = lead & 0x1FU; }
    else if (lead >= 0xE0U && lead <= 0xEFU) { length = 3U; value = lead & 0x0FU; }
    else if (lead >= 0xF0U && lead <= 0xF4U) { length = 4U; value = lead & 0x07U; }
    else return false;
    if (index + length > text.size()) return false;
    for (std::size_t offset = 1U; offset < length; ++offset) {
      const auto next = static_cast<unsigned char>(text[index + offset]);
      if ((next & 0xC0U) != 0x80U) return false;
      value = (value << 6U) | (next & 0x3FU);
    }
    if ((length == 3U && value < 0x800U) || (length == 4U && value < 0x10000U) ||
        (value >= 0xD800U && value <= 0xDFFFU) || value > 0x10FFFFU)
      return false;
    index += length;
  }
  return true;
}

// The generator's _text rule: 1..128 characters and at most 128 UTF-8 bytes
// without control characters. The byte bound implies the character bound.
bool boundedText(std::string_view value) {
  return !value.empty() && value.size() <= 128U && validUtf8(value) &&
      std::none_of(value.begin(), value.end(), [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte < 32U || byte == 127U;
      });
}

bool phoneticSymbol(std::string_view value) {
  return !value.empty() && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '_' || c == '@' || c == '+' || c == '.' || c == '-';
  });
}

struct Sequence final {
  std::string kind;
  std::vector<std::string> phones;
};

std::string join(const std::vector<std::string>& values, std::string_view separator) {
  std::string result;
  for (std::size_t index = 0U; index < values.size(); ++index) {
    if (index != 0U) result += separator;
    result += values[index];
  }
  return result;
}

std::string baseKey(const Sequence& sequence) {
  return sequence.kind + ":" + join(sequence.phones, ":");
}

std::vector<Sequence> requiredSequences(const DraftInventoryProfile& profile) {
  const std::set<std::string, std::less<>> kinds(profile.includeKinds.begin(), profile.includeKinds.end());
  const auto& vowels = profile.vowels;
  std::vector<Sequence> sequences;
  if (kinds.contains("sustain"))
    for (const auto& vowel : vowels) sequences.push_back({"sustain", {vowel}});
  if (kinds.contains("release"))
    for (const auto& vowel : vowels) sequences.push_back({"release", {vowel, "R"}});
  if (kinds.contains("breath")) sequences.push_back({"breath", {"br"}});
  if (kinds.contains("glottal-attack"))
    for (const auto& vowel : vowels) sequences.push_back({"glottal-attack", {"glottal", vowel}});
  if (kinds.contains("special"))
    for (const auto& phone : profile.specialPhones) sequences.push_back({"special", {phone}});
  if (kinds.contains("cv"))
    for (const auto& consonant : profile.consonants)
      for (const auto& vowel : vowels) sequences.push_back({"cv", {consonant, vowel}});
  if (kinds.contains("vc"))
    for (const auto& vowel : vowels)
      for (const auto& consonant : profile.consonants) sequences.push_back({"vc", {vowel, consonant}});
  if (kinds.contains("vv"))
    for (const auto& left : vowels)
      for (const auto& right : vowels)
        if (left != right) sequences.push_back({"vv", {left, right}});
  return sequences;
}

core::Result<void> validateSymbols(std::string_view field, const std::vector<std::string>& values,
                                   std::size_t limit, bool phonetic) {
  const auto name = std::string{field};
  if (values.empty() || values.size() > limit ||
      !std::all_of(values.begin(), values.end(), [](const auto& value) { return boundedText(value); }))
    return core::failure(core::ErrorCode::InvalidArgument, name + " must contain bounded nonempty symbols");
  if (std::set<std::string>(values.begin(), values.end()).size() != values.size())
    return core::failure(core::ErrorCode::InvalidArgument, name + " contains duplicate symbols");
  if (phonetic && !std::all_of(values.begin(), values.end(), [](const auto& value) { return phoneticSymbol(value); }))
    return core::failure(core::ErrorCode::InvalidArgument, name + " contains an unsupported phonetic symbol");
  return core::success();
}

// Mirrors normalize_draft_profile, check for check and in the same order.
core::Result<std::vector<Sequence>> validateProfile(const DraftInventoryProfile& profile) {
  using Output = std::vector<Sequence>;
  if (!boundedText(profile.profileId)) return invalid<Output>("draft profileId is required and bounded");
  if (profile.language != "ja")
    return invalid<Output>("draft language profile is not implemented; Japanese rules cannot stand in for other languages");
  const std::array<std::pair<std::string_view, const std::vector<std::string>*>, 5U> lists{{
      {"supportedStyles", &profile.supportedStyles}, {"vowels", &profile.vowels},
      {"consonants", &profile.consonants}, {"specialPhones", &profile.specialPhones},
      {"includeKinds", &profile.includeKinds}}};
  for (const auto& [field, values] : lists) {
    const auto checked = validateSymbols(field, *values, field == "supportedStyles" ? 16U : 64U,
        field == "vowels" || field == "consonants" || field == "specialPhones");
    if (!checked) return core::Result<Output>{checked.error()};
  }
  if (std::any_of(profile.includeKinds.begin(), profile.includeKinds.end(), [](const auto& kind) {
        return std::find(kKinds.begin(), kKinds.end(), kind) == kKinds.end();
      }))
    return invalid<Output>("includeKinds contains unsupported kinds");
  const auto& layers = profile.pitchLayers;
  if ((layers.size() != 2U && layers.size() != 3U) ||
      std::any_of(layers.begin(), layers.end(), [](auto layer) { return layer < 24 || layer > 96; }) ||
      std::set<std::int64_t>(layers.begin(), layers.end()).size() != layers.size())
    return invalid<Output>("pitchLayers requires two or three distinct bounded MIDI values");
  if (!(24 <= profile.requestedMinMidi && profile.requestedMinMidi < profile.requestedMaxMidi &&
        profile.requestedMaxMidi <= 96))
    return invalid<Output>("requestedRange must contain increasing bounded MIDI values");
  if (std::any_of(layers.begin(), layers.end(), [&](auto layer) {
        return layer < profile.requestedMinMidi || layer > profile.requestedMaxMidi;
      }))
    return invalid<Output>("pitch layers must lie inside the requested, unassessed range");
  if (profile.alternateTakes < 1 || profile.alternateTakes > 8) return invalid<Output>("alternateTakes exceeds bounds");
  if (profile.sessionBlockSize < 1 || profile.sessionBlockSize > 1024) return invalid<Output>("sessionBlockSize exceeds bounds");
  auto sequences = requiredSequences(profile);
  if (sequences.empty()) return invalid<Output>("includeKinds must produce at least one required sequence");
  std::set<std::string> keys;
  for (const auto& sequence : sequences) keys.insert(baseKey(sequence));
  if (keys.size() != sequences.size()) return invalid<Output>("phone symbols produce ambiguous coverage keys");
  // Every factor is bounded (sequences by the 64-symbol lists, 3 layers, 16
  // styles, 8 alternates), so the product cannot overflow.
  const auto count = sequences.size() * layers.size() * profile.supportedStyles.size() *
      static_cast<std::size_t>(profile.alternateTakes);
  if (count > kDraftInventoryMaximumUnits)
    return invalid<Output>("draft inventory exceeds " + std::to_string(kDraftInventoryMaximumUnits) + " units");
  return sequences;
}

J stringArray(const std::vector<std::string>& values) {
  J::Array array;
  array.reserve(values.size());
  for (const auto& value : values) array.emplace_back(value);
  return J{std::move(array)};
}

J profileObject(const DraftInventoryProfile& profile) {
  J::Array layers;
  for (const auto layer : profile.pitchLayers) layers.emplace_back(layer);
  return J{J::Object{
      {"profileId", profile.profileId}, {"language", profile.language},
      {"supportedStyles", stringArray(profile.supportedStyles)}, {"vowels", stringArray(profile.vowels)},
      {"consonants", stringArray(profile.consonants)}, {"specialPhones", stringArray(profile.specialPhones)},
      {"includeKinds", stringArray(profile.includeKinds)}, {"pitchLayers", J{std::move(layers)}},
      {"requestedRange", J{J::Object{{"minMidi", profile.requestedMinMidi}, {"maxMidi", profile.requestedMaxMidi}}}},
      {"alternateTakes", profile.alternateTakes}, {"sessionBlockSize", profile.sessionBlockSize}}};
}

std::string padded(std::size_t value, std::size_t width) {
  auto text = std::to_string(value);
  if (text.size() < width) text.insert(0U, width - text.size(), '0');
  return text;
}

// csv.writer QUOTE_MINIMAL with lineterminator "\n": quote a field holding the
// delimiter, the quote character or a line break, and double embedded quotes.
std::string csvField(std::string_view value) {
  if (value.find_first_of(",\"\r\n") == std::string_view::npos) return std::string{value};
  std::string quoted{"\""};
  for (const char c : value) {
    if (c == '"') quoted += '"';
    quoted += c;
  }
  quoted += '"';
  return quoted;
}

std::string operatorCsv(const std::vector<DraftInventoryUnit>& units, std::string_view language) {
  std::string csv;
  for (std::size_t index = 0U; index < kCsvFields.size(); ++index) {
    if (index != 0U) csv += ',';
    csv += kCsvFields[index];
  }
  csv += '\n';
  for (const auto& unit : units) {
    const auto phones = join(unit.phones, " ");
    const std::array<std::string, 11U> row{unit.promptId, unit.takeId, std::string{language}, unit.style,
        unit.kind, phones, phones, std::to_string(unit.pitchLayer), std::to_string(unit.sessionBlock),
        unit.retakeGroup, unit.filename};
    for (std::size_t index = 0U; index < row.size(); ++index) {
      if (index != 0U) csv += ',';
      csv += csvField(row[index]);
    }
    csv += '\n';
  }
  return csv;
}

core::Result<std::string> readString(const J& value, std::string_view message) {
  if (!value.isString()) return invalid<std::string>(std::string{message});
  return value.asString();
}

core::Result<std::vector<std::string>> readStrings(const J& value, std::string_view field) {
  using Output = std::vector<std::string>;
  const auto message = std::string{field} + " must contain bounded nonempty symbols";
  if (!value.isArray()) return invalid<Output>(message);
  Output values;
  for (const auto& item : value.asArray()) {
    if (!item.isString()) return invalid<Output>(message);
    values.push_back(item.asString());
  }
  return values;
}

core::Result<std::int64_t> readInteger(const J& value, std::string_view message) {
  if (!value.isInteger()) return invalid<std::int64_t>(std::string{message});
  return value.asInt64();
}

core::Result<DraftInventoryProfile> readProfile(const J::Object& object, bool requireEveryField) {
  using Output = DraftInventoryProfile;
  for (const auto& [key, value] : object) {
    static_cast<void>(value);
    if (std::find(kProfileFields.begin(), kProfileFields.end(), key) == kProfileFields.end())
      return invalid<Output>("draft profile contains unknown fields; rangeTest/PASS is not draft authority");
  }
  if (requireEveryField)
    for (const auto field : kProfileFields)
      if (object.find(field) == object.end())
        return invalid<Output>("invalid draft profile: missing " + std::string{field});
  DraftInventoryProfile profile;
  const auto field = [&object](std::string_view name) -> const J* {
    const auto found = object.find(name);
    return found == object.end() ? nullptr : &found->second;
  };
  if (const auto* value = field("profileId")) {
    auto read = readString(*value, "draft profileId is required and bounded");
    if (!read) return core::Result<Output>{read.error()};
    profile.profileId = std::move(read.value());
  }
  if (const auto* value = field("language")) {
    auto read = readString(*value,
        "draft language profile is not implemented; Japanese rules cannot stand in for other languages");
    if (!read) return core::Result<Output>{read.error()};
    profile.language = std::move(read.value());
  }
  const std::array<std::pair<std::string_view, std::vector<std::string>*>, 5U> lists{{
      {"supportedStyles", &profile.supportedStyles}, {"vowels", &profile.vowels},
      {"consonants", &profile.consonants}, {"specialPhones", &profile.specialPhones},
      {"includeKinds", &profile.includeKinds}}};
  for (const auto& [name, target] : lists) {
    if (const auto* value = field(name)) {
      auto read = readStrings(*value, name);
      if (!read) return core::Result<Output>{read.error()};
      *target = std::move(read.value());
    }
  }
  if (const auto* value = field("pitchLayers")) {
    constexpr std::string_view message{"pitchLayers requires two or three distinct bounded MIDI values"};
    if (!value->isArray()) return invalid<Output>(std::string{message});
    profile.pitchLayers.clear();
    for (const auto& item : value->asArray()) {
      auto read = readInteger(item, message);
      if (!read) return core::Result<Output>{read.error()};
      profile.pitchLayers.push_back(read.value());
    }
  }
  if (const auto* value = field("requestedRange")) {
    const std::string message{"requestedRange must contain increasing bounded MIDI values"};
    if (!value->isObject() || value->asObject().size() != 2U) return invalid<Output>(message);
    const auto& range = value->asObject();
    const auto minimum = range.find("minMidi"), maximum = range.find("maxMidi");
    if (minimum == range.end() || maximum == range.end() || !minimum->second.isInteger() ||
        !maximum->second.isInteger())
      return invalid<Output>(message);
    profile.requestedMinMidi = minimum->second.asInt64();
    profile.requestedMaxMidi = maximum->second.asInt64();
  }
  for (const auto& [name, target] : std::array<std::pair<std::string_view, std::int64_t*>, 2U>{{
           {"alternateTakes", &profile.alternateTakes}, {"sessionBlockSize", &profile.sessionBlockSize}}}) {
    if (const auto* value = field(name)) {
      auto read = readInteger(*value, std::string{name} + " exceeds bounds");
      if (!read) return core::Result<Output>{read.error()};
      *target = read.value();
    }
  }
  return profile;
}

struct Generated final {
  DraftInventory inventory;
  J document;
};

core::Result<Generated> generate(const DraftInventoryProfile& profile) {
  auto sequences = validateProfile(profile);
  if (!sequences) return core::Result<Generated>{sequences.error()};
  Generated generated;
  auto& inventory = generated.inventory;
  inventory.profile = profile;
  J::Array units, coverage;
  for (const auto& sequence : sequences.value()) {
    inventory.requiredCoverage.push_back(baseKey(sequence));
    coverage.emplace_back(inventory.requiredCoverage.back());
  }
  const auto blockSize = static_cast<std::size_t>(profile.sessionBlockSize);
  for (const auto& style : profile.supportedStyles) {
    // Hash the exact style, not a lossy slug; two labels may slug alike.
    const auto styleKey = core::sha256Hex(style).substr(0U, 16U);
    for (const auto& sequence : sequences.value()) {
      const auto coverageKey = baseKey(sequence);
      for (const auto layer : profile.pitchLayers) {
        const auto identity = core::sha256Hex(formats::stringifyJson(
            J{J::Array{profile.language, style, coverageKey, layer}}, false));
        for (std::int64_t alternate = 1; alternate <= profile.alternateTakes; ++alternate) {
          DraftInventoryUnit unit;
          unit.promptId = "P" + padded(inventory.units.size() + 1U, 5U) + "-" + identity;
          unit.takeId = unit.promptId + "-t" + padded(static_cast<std::size_t>(alternate), 2U);
          unit.style = style;
          unit.kind = sequence.kind;
          unit.phones = sequence.phones;
          unit.pitchLayer = layer;
          unit.sessionBlock = static_cast<std::int64_t>(1U + inventory.units.size() / blockSize);
          unit.retakeGroup = "rt-" + identity;
          unit.filename = "takes/s" + styleKey + "/p" + std::to_string(layer) + "/" + unit.takeId + ".wav";
          unit.coverageKey = coverageKey;
          unit.assignmentId = identity;
          const auto phones = join(unit.phones, " ");
          units.emplace_back(J::Object{
              {"promptId", unit.promptId}, {"takeId", unit.takeId}, {"language", profile.language},
              {"style", unit.style}, {"kind", unit.kind}, {"phones", stringArray(unit.phones)},
              {"pronunciationHint", phones}, {"pitchLayer", unit.pitchLayer},
              {"sessionBlock", unit.sessionBlock}, {"retakeGroup", unit.retakeGroup},
              {"filename", unit.filename}, {"coverageKey", unit.coverageKey},
              {"assignmentId", unit.assignmentId}});
          inventory.units.push_back(std::move(unit));
        }
      }
    }
  }
  auto payload = profileObject(profile).asObject();
  payload.emplace("schemaVersion", std::int64_t{2});
  payload.emplace("generatorVersion", std::string{kDraftInventoryGeneratorVersion});
  payload.emplace("rangeAssessment", J{J::Object{{"status", "NOT_ASSESSED"}}});
  payload.emplace("requiredCoverage", J{std::move(coverage)});
  payload.emplace("units", J{std::move(units)});
  inventory.inventorySha256 = core::sha256Hex(formats::stringifyJson(J{payload}, false));
  inventory.operatorCsv = operatorCsv(inventory.units, profile.language);
  inventory.scriptSha256 = core::sha256Hex(inventory.operatorCsv);
  payload.emplace("inventorySha256", inventory.inventorySha256);
  payload.emplace("scriptSha256", inventory.scriptSha256);
  generated.document = J{std::move(payload)};
  inventory.documentJson = formats::stringifyJson(generated.document, true);
  return generated;
}

// A draft inventory holds only integers. Python keeps 60.0 distinct from 60,
// so a fractional spelling must not pass through a numeric normalization here.
bool containsNonInteger(const J& value) {
  if (value.isNumber()) return !value.isInteger();
  if (value.isArray())
    return std::any_of(value.asArray().begin(), value.asArray().end(), containsNonInteger);
  if (value.isObject())
    return std::any_of(value.asObject().begin(), value.asObject().end(),
        [](const auto& entry) { return containsNonInteger(entry.second); });
  return false;
}

core::Result<void> nonEmptyText(std::string_view value, std::string_view label) {
  if (!boundedText(value))
    return core::failure(core::ErrorCode::InvalidArgument,
        std::string{label} + " must be 1 to 128 bytes of UTF-8 text without control characters");
  return core::success();
}

std::string stagingSuffix() {
  static std::atomic_uint64_t sequence{0U};
  std::random_device device;
  return std::to_string(device()) + "-" + std::to_string(sequence.fetch_add(1U));
}

struct StageCleanup final {
  std::filesystem::path path;
  publication::DirectoryIdentity parentIdentity, stageIdentity;
  bool published{false};
  ~StageCleanup() {
    if (!published) publication::cleanupOwnedDirectory(path, parentIdentity, stageIdentity);
  }
};
}  // namespace

core::Result<DraftInventoryProfile> parseDraftInventoryProfile(std::string_view json) {
  const auto parsed = formats::parseJson(json);
  if (!parsed) return core::Result<DraftInventoryProfile>{parsed.error()};
  if (!parsed.value().isObject()) return invalid<DraftInventoryProfile>("Draft inventory profile must be a JSON object");
  return readProfile(parsed.value().asObject(), false);
}

core::Result<DraftInventory> generateDraftInventory(const DraftInventoryProfile& profile) {
  auto generated = generate(profile);
  if (!generated) return core::Result<DraftInventory>{generated.error()};
  return std::move(generated.value().inventory);
}

core::Result<DraftInventory> loadDraftInventory(std::string_view documentJson) {
  using Output = DraftInventory;
  if (documentJson.size() > kMaximumInventoryBytes) return invalid<Output>("Draft inventory exceeds its byte bound");
  const auto parsed = formats::parseJson(documentJson);
  if (!parsed) return core::Result<Output>{parsed.error()};
  if (!parsed.value().isObject()) return invalid<Output>("draft inventory must be an object");
  const auto& object = parsed.value().asObject();
  const auto units = object.find("units");
  if (units == object.end() || !units->second.isArray() || units->second.asArray().size() > kDraftInventoryMaximumUnits)
    return invalid<Output>("draft inventory units exceed bounds or are not a list");
  if (containsNonInteger(parsed.value())) return invalid<Output>("draft inventory contains a non-integer number");
  J::Object profileFields;
  for (const auto field : kProfileFields) {
    const auto found = object.find(field);
    if (found == object.end()) return invalid<Output>("invalid draft profile: missing " + std::string{field});
    profileFields.emplace(std::string{field}, found->second);
  }
  const auto profile = readProfile(profileFields, true);
  if (!profile) return invalid<Output>("invalid draft profile: " + profile.error().message);
  auto expected = generate(profile.value());
  if (!expected) return invalid<Output>("invalid draft profile: " + expected.error().message);
  // Exact reconstruction covers coverage/style/layer/take ownership, paths,
  // the assessment and both digests, including rehashed edits.
  if (formats::stringifyJson(parsed.value(), false) == formats::stringifyJson(expected.value().document, false))
    return std::move(expected.value().inventory);
  std::string differences;
  for (const auto& [key, value] : expected.value().document.asObject()) {
    const auto actual = object.find(key);
    if (actual == object.end() ||
        formats::stringifyJson(actual->second, false) != formats::stringifyJson(value, false)) {
      if (!differences.empty()) differences += "; ";
      differences += "draft inventory field differs from deterministic profile: " + key;
    }
  }
  if (std::any_of(object.begin(), object.end(), [&](const auto& entry) {
        return !expected.value().document.asObject().contains(entry.first);
      })) {
    if (!differences.empty()) differences += "; ";
    differences += "draft inventory contains unknown fields";
  }
  return core::failure<Output>(core::ErrorCode::Conflict, "Draft inventory is not its profile's exact output", differences);
}

core::Result<VoicebankProductionProject> makeDraftProducerProject(
    const DraftInventory& inventory, std::string projectId, std::string producerId) {
  using Output = VoicebankProductionProject;
  if (const auto checked = nonEmptyText(projectId, "Project ID"); !checked) return core::Result<Output>{checked.error()};
  if (const auto checked = nonEmptyText(producerId, "Producer ID"); !checked) return core::Result<Output>{checked.error()};
  if (inventory.units.empty() || inventory.inventorySha256.size() != 64U)
    return invalid<Output>("Generate or load a draft inventory before creating its producer");
  VoicebankProductionProject project;
  project.schemaVersion = kProductionStyleSchemaVersion;
  project.projectId = std::move(projectId);
  project.inventoryId = inventory.profile.profileId;
  project.inventorySha256 = inventory.inventorySha256;
  project.immutableAssetRoot = "assets";
  project.lifecycle = ProductionLifecycle::Draft;
  project.language = inventory.profile.language;
  project.operators = {{std::move(producerId), "PRODUCER"}};
  std::set<std::string, std::less<>> assignments;
  for (const auto& unit : inventory.units) {
    // Language belongs to the immutable producer, not each row; the first
    // alternate names the planned take, as the external generator does.
    if (!assignments.insert(unit.assignmentId).second) continue;
    project.unitAssignments.push_back({.coverageKey = unit.coverageKey,
        .pitchLayer = static_cast<std::int32_t>(unit.pitchLayer), .promptId = unit.promptId,
        .plannedTakeId = unit.takeId, .takeId = {}, .state = UnitQueueState::Missing,
        .markerReviewed = false, .pitchReviewed = false, .style = unit.style});
  }
  return project;
}

core::Result<CreatedDraftProducerWorkspace> createDraftProducerWorkspace(
    const std::filesystem::path& destination, const DraftInventoryProfile& profile,
    std::string projectId, std::string producerId, std::string occurredAtUtc, std::stop_token stop) {
  using Output = CreatedDraftProducerWorkspace;
  if (destination.empty() || !destination.is_absolute() || destination != destination.lexically_normal() ||
      destination.filename().empty() || destination.filename().string().front() == '.' ||
      destination == destination.root_path())
    return invalid<Output>("Choose an absolute, visible new folder for the producer workspace");
  if (!isProductionUtcTimestamp(occurredAtUtc)) return invalid<Output>("Workspace creation timestamp is invalid");
  auto inventory = generateDraftInventory(profile);
  if (!inventory) return core::Result<Output>{inventory.error()};
  auto project = makeDraftProducerProject(inventory.value(), projectId, producerId);
  if (!project) return core::Result<Output>{project.error()};
  auto checked = publication::cancelled(stop);
  if (!checked) return core::Result<Output>{checked.error()};
  const auto parent = publication::realDirectory(destination.parent_path());
  if (!parent) return core::Result<Output>{parent.error()};
  const auto finalPath = parent.value() / destination.filename();
  const auto parentIdentity = publication::captureDirectoryIdentity(parent.value());
  if (!parentIdentity) return core::Result<Output>{parentIdentity.error()};
  if (!publication::absentDestination(finalPath))
    return core::failure<Output>(core::ErrorCode::Conflict,
        "The producer workspace folder already exists; choose a new folder name", finalPath.string());
  // Exclusive creation; a private stage is never reused, even after a crash.
  const auto stage = parent.value() / (".seam-producer-" +
      core::sha256Hex(finalPath.generic_string()).substr(0U, 16U) + "-" + stagingSuffix());
  std::error_code error;
  if (!std::filesystem::create_directory(stage, error) || error)
    return core::failure<Output>(core::ErrorCode::Conflict, "Cannot create private producer staging", stage.string());
  const auto stageIdentity = publication::captureDirectoryIdentity(stage);
  if (!stageIdentity) {
    std::filesystem::remove(stage, error);
    return core::Result<Output>{stageIdentity.error()};
  }
  StageCleanup cleanup{stage, parentIdentity.value(), stageIdentity.value()};
  std::filesystem::permissions(stage, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, error);
  if (error) return core::failure<Output>(core::ErrorCode::IoError, "Cannot restrict producer staging", error.message());
  checked = core::durableAtomicWriteTextNew(stage / kDraftProducerInventoryFile, inventory.value().documentJson);
  if (!checked) return core::Result<Output>{checked.error()};
  checked = core::durableAtomicWriteTextNew(stage / kDraftProducerScriptFile, inventory.value().operatorCsv);
  if (!checked) return core::Result<Output>{checked.error()};
  checked = publication::cancelled(stop);
  if (!checked) return core::Result<Output>{checked.error()};
  ProductionProjectRepository stagedRepository{stage / kDraftProducerWorkspaceDirectory};
  checked = stagedRepository.initialize(project.value(),
      {.action = "create", .subjectId = project.value().projectId, .operatorId = producerId,
       .occurredAtUtc = occurredAtUtc});
  if (!checked) return core::Result<Output>{checked.error()};
  checked = publication::syncDirectory(stage);
  if (!checked) return core::Result<Output>{checked.error()};
  checked = publication::cancelled(stop);
  if (!checked) return core::Result<Output>{checked.error()};
  checked = publication::publishNewDirectory(stage, finalPath, parentIdentity.value(), stageIdentity.value());
  if (!checked) return core::Result<Output>{checked.error()};
  cleanup.published = true;
  Output result{.root = finalPath, .producerRoot = finalPath / kDraftProducerWorkspaceDirectory,
      .inventoryPath = finalPath / kDraftProducerInventoryFile, .scriptPath = finalPath / kDraftProducerScriptFile,
      .projectId = project.value().projectId, .producerId = std::move(producerId),
      .inventorySha256 = inventory.value().inventorySha256, .scriptSha256 = inventory.value().scriptSha256,
      .projectSha256 = {}, .generation = 0U, .units = inventory.value().units.size(),
      .assignments = project.value().unitAssignments.size(), .durabilityConfirmed = true,
      .diagnostics = {"Draft inventory generated; its requested range is NOT_ASSESSED. No source, take or review exists yet."}};
  checked = publication::syncDirectory(parent.value());
  if (!checked) {
    result.durabilityConfirmed = false;
    result.diagnostics.push_back("Workspace created but parent durability is uncertain. Reopen it before retrying; do not create it again. " +
        checked.error().message);
  }
  const auto reopened = ProductionProjectRepository{result.producerRoot}.recover();
  if (!reopened) return core::Result<Output>{reopened.error()};
  if (reopened.value().inventorySha256 != result.inventorySha256 || reopened.value().projectId != result.projectId)
    return core::failure<Output>(core::ErrorCode::Conflict, "Created producer workspace does not match its inventory");
  result.generation = reopened.value().lastDurableGeneration;
  result.projectSha256 = core::sha256Hex(encodeProductionProject(reopened.value()));
  return result;
}

core::Result<DraftProducerWorkspaceInventory> readDraftProducerWorkspaceInventory(const std::filesystem::path& root) {
  using Output = DraftProducerWorkspaceInventory;
  const auto directory = publication::realDirectory(root);
  if (!directory) return core::Result<Output>{directory.error()};
  const auto text = core::readTextFileLimited(directory.value() / kDraftProducerInventoryFile, kMaximumInventoryBytes);
  if (!text) return core::Result<Output>{text.error()};
  auto inventory = loadDraftInventory(text.value());
  if (!inventory) return core::Result<Output>{inventory.error()};
  const auto producer = publication::realDirectory(directory.value() / kDraftProducerWorkspaceDirectory);
  if (!producer) return core::Result<Output>{producer.error()};
  return Output{.root = directory.value(), .producerRoot = producer.value(), .inventory = std::move(inventory.value())};
}

}  // namespace seam::voicebank_production
