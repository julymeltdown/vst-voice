#include "seam/voicebank/manifest_json.hpp"

#include "seam/formats/json_value.hpp"

#include <fstream>
#include <limits>
#include <sstream>

namespace seam::voicebank {
namespace {

using seam::formats::JsonValue;
using Object = JsonValue::Object;
using Array = JsonValue::Array;

std::string languageName(domain::Language language) {
  switch (language) {
    case domain::Language::Japanese: return "ja";
    case domain::Language::Korean: return "ko";
    case domain::Language::English: return "en";
    case domain::Language::Unspecified: return "und";
  }
  return "und";
}

domain::Language parseLanguage(std::string_view value) noexcept {
  if (value == "ja") return domain::Language::Japanese;
  if (value == "ko") return domain::Language::Korean;
  if (value == "en") return domain::Language::English;
  return domain::Language::Unspecified;
}

JsonValue encodeMarkers(const UnitMarkers& markers) {
  Object object{
      {"audioOffset", JsonValue{markers.audioOffset}},
      {"consonantEnd", JsonValue{markers.consonantEnd}},
      {"vowelOnset", JsonValue{markers.vowelOnset}},
      {"stableStart", JsonValue{markers.stableStart}},
      {"audioEnd", JsonValue{markers.audioEnd}},
  };
  if (markers.loopStart.has_value()) object.emplace("loopStart", JsonValue{*markers.loopStart});
  if (markers.loopEnd.has_value()) object.emplace("loopEnd", JsonValue{*markers.loopEnd});
  if (markers.releaseStart.has_value()) {
    object.emplace("releaseStart", JsonValue{*markers.releaseStart});
  }
  return JsonValue{std::move(object)};
}

JsonValue encodeManifest(const Manifest& manifest) {
  Array styles;
  for (const auto& style : manifest.styles) styles.emplace_back(style);
  Array units;
  for (const auto& unit : manifest.units) {
    Array phones;
    for (const auto& phone : unit.phones) phones.emplace_back(phone);
    units.emplace_back(Object{
        {"id", JsonValue{unit.id}},
        {"alias", JsonValue{unit.alias}},
        {"phones", JsonValue{std::move(phones)}},
        {"kind", JsonValue{std::string(unitKindName(unit.kind))}},
        {"audio", JsonValue{unit.audioPath.generic_string()}},
        {"rootMidi", JsonValue{static_cast<std::int64_t>(unit.rootMidi)}},
        {"style", JsonValue{unit.style}},
        {"take", JsonValue{static_cast<std::int64_t>(unit.take)}},
        {"priority", JsonValue{static_cast<std::int64_t>(unit.priority)}},
        {"gainDb", JsonValue{static_cast<double>(unit.gainDb)}},
        {"renderer", JsonValue{std::string(rendererHintName(unit.renderer))}},
        {"enabled", JsonValue{unit.enabled}},
        {"markers", encodeMarkers(unit.markers)},
    });
  }
  return JsonValue{Object{
      {"formatId", JsonValue{"com.project-seam.voicebank"}},
      {"schemaVersion", JsonValue{static_cast<std::int64_t>(Manifest::kSchemaVersion)}},
      {"id", JsonValue{manifest.id}},
      {"version", JsonValue{manifest.version}},
      {"displayName", JsonValue{manifest.displayName}},
      {"language", JsonValue{languageName(manifest.language)}},
      {"expectedSampleRate", JsonValue{static_cast<std::int64_t>(manifest.expectedSampleRate)}},
      {"styles", JsonValue{std::move(styles)}},
      {"units", JsonValue{std::move(units)}},
  }};
}

core::Result<std::int64_t> integerField(const JsonValue& object,
                                        std::string_view key,
                                        bool optional,
                                        std::int64_t defaultValue = 0) {
  const auto* value = object.find(key);
  if (value == nullptr) {
    if (optional) return defaultValue;
    return core::failure<std::int64_t>(core::ErrorCode::ParseError,
                                       "Missing integer field: " + std::string(key));
  }
  if (!value->isNumber()) {
    return core::failure<std::int64_t>(core::ErrorCode::ParseError,
                                       "Field must be an integer: " + std::string(key));
  }
  try {
    return value->asInt64();
  } catch (...) {
    return core::failure<std::int64_t>(core::ErrorCode::ParseError,
                                       "Field is not a representable integer: " +
                                           std::string(key));
  }
}

core::Result<UnitMarkers> decodeMarkers(const JsonValue& value) {
  if (!value.isObject()) {
    return core::failure<UnitMarkers>(core::ErrorCode::ParseError,
                                      "Unit markers must be an object");
  }
  auto audioOffset = integerField(value, "audioOffset", false);
  auto consonantEnd = integerField(value, "consonantEnd", false);
  auto vowelOnset = integerField(value, "vowelOnset", false);
  auto stableStart = integerField(value, "stableStart", false);
  auto audioEnd = integerField(value, "audioEnd", false);
  if (!audioOffset) return core::Result<UnitMarkers>{audioOffset.error()};
  if (!consonantEnd) return core::Result<UnitMarkers>{consonantEnd.error()};
  if (!vowelOnset) return core::Result<UnitMarkers>{vowelOnset.error()};
  if (!stableStart) return core::Result<UnitMarkers>{stableStart.error()};
  if (!audioEnd) return core::Result<UnitMarkers>{audioEnd.error()};
  UnitMarkers markers{
      .audioOffset = audioOffset.value(),
      .consonantEnd = consonantEnd.value(),
      .vowelOnset = vowelOnset.value(),
      .stableStart = stableStart.value(),
      .loopStart = std::nullopt,
      .loopEnd = std::nullopt,
      .releaseStart = std::nullopt,
      .audioEnd = audioEnd.value(),
  };
  if (value.find("loopStart") != nullptr) {
    auto parsed = integerField(value, "loopStart", false);
    if (!parsed) return core::Result<UnitMarkers>{parsed.error()};
    markers.loopStart = parsed.value();
  }
  if (value.find("loopEnd") != nullptr) {
    auto parsed = integerField(value, "loopEnd", false);
    if (!parsed) return core::Result<UnitMarkers>{parsed.error()};
    markers.loopEnd = parsed.value();
  }
  if (value.find("releaseStart") != nullptr) {
    auto parsed = integerField(value, "releaseStart", false);
    if (!parsed) return core::Result<UnitMarkers>{parsed.error()};
    markers.releaseStart = parsed.value();
  }
  return markers;
}

core::Result<Manifest> decodeManifest(const JsonValue& root) {
  if (!root.isObject()) {
    return core::failure<Manifest>(core::ErrorCode::ParseError,
                                   "Voicebank manifest root must be an object");
  }
  const auto* formatId = root.find("formatId");
  const auto* schema = root.find("schemaVersion");
  const auto* id = root.find("id");
  const auto* version = root.find("version");
  const auto* displayName = root.find("displayName");
  const auto* language = root.find("language");
  const auto* expectedSampleRate = root.find("expectedSampleRate");
  const auto* styles = root.find("styles");
  const auto* units = root.find("units");
  if (formatId == nullptr || schema == nullptr || id == nullptr || version == nullptr ||
      displayName == nullptr || language == nullptr || expectedSampleRate == nullptr ||
      styles == nullptr || units == nullptr || !formatId->isString() || !schema->isNumber() ||
      !id->isString() || !version->isString() || !displayName->isString() ||
      !language->isString() || !expectedSampleRate->isNumber() || !styles->isArray() ||
      !units->isArray()) {
    return core::failure<Manifest>(core::ErrorCode::ParseError,
                                   "Voicebank manifest fields are invalid");
  }
  if (formatId->asString() != "com.project-seam.voicebank") {
    return core::failure<Manifest>(core::ErrorCode::Unsupported,
                                   "Unsupported voicebank manifest format");
  }
  if (schema->asInt64() != Manifest::kSchemaVersion) {
    return core::failure<Manifest>(core::ErrorCode::Unsupported,
                                   "Unsupported voicebank manifest schema");
  }
  const auto sampleRateValue = expectedSampleRate->asInt64();
  if (sampleRateValue < 0 ||
      sampleRateValue > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
    return core::failure<Manifest>(core::ErrorCode::ParseError,
                                   "Voicebank sample rate is invalid");
  }
  Manifest manifest;
  manifest.id = id->asString();
  manifest.version = version->asString();
  manifest.displayName = displayName->asString();
  manifest.language = parseLanguage(language->asString());
  manifest.expectedSampleRate = static_cast<std::uint32_t>(sampleRateValue);
  manifest.styles.clear();
  for (const auto& style : styles->asArray()) {
    if (!style.isString()) {
      return core::failure<Manifest>(core::ErrorCode::ParseError,
                                     "Voicebank style must be a string");
    }
    manifest.styles.push_back(style.asString());
  }

  for (const auto& unitJson : units->asArray()) {
    if (!unitJson.isObject()) {
      return core::failure<Manifest>(core::ErrorCode::ParseError,
                                     "Voicebank unit must be an object");
    }
    const auto* unitId = unitJson.find("id");
    const auto* alias = unitJson.find("alias");
    const auto* phones = unitJson.find("phones");
    const auto* kind = unitJson.find("kind");
    const auto* audio = unitJson.find("audio");
    const auto* style = unitJson.find("style");
    const auto* gainDb = unitJson.find("gainDb");
    const auto* renderer = unitJson.find("renderer");
    const auto* enabled = unitJson.find("enabled");
    const auto* markersJson = unitJson.find("markers");
    if (unitId == nullptr || alias == nullptr || phones == nullptr || kind == nullptr ||
        audio == nullptr || style == nullptr || gainDb == nullptr || renderer == nullptr ||
        enabled == nullptr || markersJson == nullptr || !unitId->isString() ||
        !alias->isString() || !phones->isArray() || !kind->isString() ||
        !audio->isString() || !style->isString() || !gainDb->isNumber() ||
        !renderer->isString() || !enabled->isBool()) {
      return core::failure<Manifest>(core::ErrorCode::ParseError,
                                     "Voicebank unit fields are invalid");
    }
    auto rootMidi = integerField(unitJson, "rootMidi", false);
    auto take = integerField(unitJson, "take", false);
    auto priority = integerField(unitJson, "priority", true, 0);
    auto markers = decodeMarkers(*markersJson);
    if (!rootMidi) return core::Result<Manifest>{rootMidi.error()};
    if (!take) return core::Result<Manifest>{take.error()};
    if (!priority) return core::Result<Manifest>{priority.error()};
    if (!markers) return core::Result<Manifest>{markers.error()};
    Unit unit;
    unit.id = unitId->asString();
    unit.alias = alias->asString();
    for (const auto& phone : phones->asArray()) {
      if (!phone.isString()) {
        return core::failure<Manifest>(core::ErrorCode::ParseError,
                                       "Voicebank phone must be a string");
      }
      unit.phones.push_back(phone.asString());
    }
    unit.kind = parseUnitKind(kind->asString());
    unit.audioPath = std::filesystem::path(audio->asString());
    unit.rootMidi = static_cast<std::int32_t>(rootMidi.value());
    unit.style = style->asString();
    unit.take = static_cast<std::int32_t>(take.value());
    unit.priority = static_cast<std::int32_t>(priority.value());
    unit.gainDb = static_cast<float>(gainDb->asNumber());
    unit.renderer = parseRendererHint(renderer->asString());
    unit.markers = markers.value();
    unit.enabled = enabled->asBool();
    manifest.units.push_back(std::move(unit));
  }
  const auto validation = manifest.validate();
  if (!validation) return core::Result<Manifest>{validation.error()};
  return manifest;
}

}  // namespace

core::Result<std::string> ManifestJsonCodec::encode(const Manifest& manifest) const {
  const auto validation = manifest.validate();
  if (!validation) return core::Result<std::string>{validation.error()};
  return seam::formats::stringifyJson(encodeManifest(manifest), true);
}

core::Result<Manifest> ManifestJsonCodec::decode(std::string_view json) const {
  auto parsed = seam::formats::parseJson(json);
  if (!parsed) return core::Result<Manifest>{parsed.error()};
  try {
    return decodeManifest(parsed.value());
  } catch (const std::exception& exception) {
    return core::failure<Manifest>(core::ErrorCode::ParseError,
                                   "Voicebank manifest contains a value of the wrong type",
                                   exception.what());
  }
}

core::Result<void> ManifestJsonCodec::save(const Manifest& manifest,
                                           const std::filesystem::path& path) const {
  auto encoded = encode(manifest);
  if (!encoded) return core::Result<void>{encoded.error()};
  std::error_code error;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create voicebank directory",
                           error.message());
    }
  }
  const auto temporary = path.string() + ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create voicebank manifest",
                           temporary);
    }
    stream << encoded.value();
    stream.flush();
    if (!stream) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to write voicebank manifest",
                           temporary);
    }
  }
  std::filesystem::remove(path, error);
  error.clear();
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary);
    return core::failure(core::ErrorCode::IoError,
                         "Unable to replace voicebank manifest",
                         error.message());
  }
  return core::success();
}

core::Result<Manifest> ManifestJsonCodec::load(const std::filesystem::path& path) const {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return core::failure<Manifest>(core::ErrorCode::IoError,
                                   "Unable to open voicebank manifest",
                                   path.string());
  }
  std::ostringstream content;
  content << stream.rdbuf();
  if (!stream.good() && !stream.eof()) {
    return core::failure<Manifest>(core::ErrorCode::IoError,
                                   "Unable to read voicebank manifest",
                                   path.string());
  }
  return decode(content.str());
}

}  // namespace seam::voicebank
