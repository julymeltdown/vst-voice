#include "seam/character/character.hpp"

#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"

#include <array>
#include <cctype>
#include <system_error>

namespace seam::character {
namespace {

constexpr std::array<State, 6> kStates{
    State::Neutral, State::Focused, State::Rendering,
    State::Complete, State::Warning, State::Error};

bool safeRelativeAsset(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) return false;
  for (const auto& part : path) {
    if (part == "." || part == "..") return false;
  }
  return true;
}

core::Result<std::string> requiredString(const formats::JsonValue& root,
                                         std::string_view key) {
  const auto* value = root.find(key);
  if (value == nullptr || !value->isString() || value->asString().empty()) {
    return core::failure<std::string>(core::ErrorCode::ParseError,
                                      "Character manifest string is missing",
                                      std::string{key});
  }
  return value->asString();
}

bool validHexColor(std::string_view value) {
  if (value.size() != 7U || value.front() != '#') return false;
  for (std::size_t index = 1U; index < value.size(); ++index) {
    if (std::isxdigit(static_cast<unsigned char>(value[index])) == 0) return false;
  }
  return true;
}

}  // namespace

std::string_view stateName(State state) noexcept {
  switch (state) {
    case State::Neutral: return "neutral";
    case State::Focused: return "focused";
    case State::Rendering: return "rendering";
    case State::Complete: return "complete";
    case State::Warning: return "warning";
    case State::Error: return "error";
  }
  return "neutral";
}

State parseState(std::string_view value) noexcept {
  if (value == "focused") return State::Focused;
  if (value == "rendering") return State::Rendering;
  if (value == "complete") return State::Complete;
  if (value == "warning") return State::Warning;
  if (value == "error") return State::Error;
  return State::Neutral;
}

core::Result<void> Manifest::validate() const {
  if (schemaVersion != 1) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Unsupported character manifest schema",
                         std::to_string(schemaVersion));
  }
  if (characterId.empty() || displayName.empty() || version.empty() ||
      voicebankId.empty() || style.empty()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Character manifest identity fields must not be empty");
  }
  if (!validHexColor(accent.primary) || !validHexColor(accent.secondary)) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Character accent colors must use #RRGGBB");
  }
  for (const auto state : kStates) {
    const auto iterator = stateAssets.find(state);
    if (iterator == stateAssets.end() || !safeRelativeAsset(iterator->second)) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Character manifest state asset is missing or unsafe",
                           std::string{stateName(state)});
    }
  }
  return core::success();
}

std::filesystem::path Manifest::assetFor(State state) const {
  const auto iterator = stateAssets.find(state);
  if (iterator != stateAssets.end()) return iterator->second;
  const auto fallback = stateAssets.find(defaultState);
  return fallback == stateAssets.end() ? std::filesystem::path{} : fallback->second;
}

std::filesystem::path Package::assetPath(State state) const {
  return root / manifest.assetFor(state);
}

core::Result<Package> loadPackage(const std::filesystem::path& packageRoot,
                                  std::uint64_t maximumManifestBytes) {
  if (packageRoot.empty()) {
    return core::failure<Package>(core::ErrorCode::InvalidArgument,
                                  "Character package root is empty");
  }
  std::error_code error;
  const auto canonicalRoot = std::filesystem::weakly_canonical(packageRoot, error);
  if (error || !std::filesystem::is_directory(canonicalRoot, error)) {
    return core::failure<Package>(core::ErrorCode::IoError,
                                  "Character package root is not a readable directory",
                                  packageRoot.string());
  }
  auto text = core::readTextFileLimited(canonicalRoot / "manifest.json",
                                        maximumManifestBytes);
  if (!text) return core::Result<Package>{text.error()};
  auto parsed = formats::parseJson(text.value(), formats::JsonParseLimits{
      .maximumInputBytes = static_cast<std::size_t>(maximumManifestBytes),
      .maximumDepth = 16U,
      .maximumNodes = 512U,
      .maximumStringBytes = 64U * 1024U,
      .maximumCollectionEntries = 128U,
  });
  if (!parsed) return core::Result<Package>{parsed.error()};
  if (!parsed.value().isObject()) {
    return core::failure<Package>(core::ErrorCode::ParseError,
                                  "Character manifest root must be an object");
  }

  Manifest manifest;
  if (const auto* schema = parsed.value().find("schemaVersion");
      schema != nullptr && schema->isNumber()) {
    manifest.schemaVersion = static_cast<std::int32_t>(schema->asInt64());
  }
  auto id = requiredString(parsed.value(), "characterId");
  auto name = requiredString(parsed.value(), "displayName");
  auto version = requiredString(parsed.value(), "version");
  auto voicebank = requiredString(parsed.value(), "voicebankId");
  auto style = requiredString(parsed.value(), "style");
  if (!id) return core::Result<Package>{id.error()};
  if (!name) return core::Result<Package>{name.error()};
  if (!version) return core::Result<Package>{version.error()};
  if (!voicebank) return core::Result<Package>{voicebank.error()};
  if (!style) return core::Result<Package>{style.error()};
  manifest.characterId = std::move(id.value());
  manifest.displayName = std::move(name.value());
  manifest.version = std::move(version.value());
  manifest.voicebankId = std::move(voicebank.value());
  manifest.style = std::move(style.value());
  if (const auto* defaultState = parsed.value().find("defaultState");
      defaultState != nullptr && defaultState->isString()) {
    manifest.defaultState = parseState(defaultState->asString());
  }
  if (const auto* accent = parsed.value().find("accent");
      accent != nullptr && accent->isObject()) {
    if (const auto* primary = accent->find("primary");
        primary != nullptr && primary->isString()) {
      manifest.accent.primary = primary->asString();
    }
    if (const auto* secondary = accent->find("secondary");
        secondary != nullptr && secondary->isString()) {
      manifest.accent.secondary = secondary->asString();
    }
  }
  const auto* states = parsed.value().find("states");
  if (states == nullptr || !states->isObject()) {
    return core::failure<Package>(core::ErrorCode::ParseError,
                                  "Character manifest states object is missing");
  }
  for (const auto state : kStates) {
    const auto key = stateName(state);
    const auto* value = states->find(key);
    if (value == nullptr || !value->isString()) {
      return core::failure<Package>(core::ErrorCode::ParseError,
                                    "Character state asset is missing",
                                    std::string{key});
    }
    manifest.stateAssets.emplace(state, std::filesystem::path{value->asString()});
  }
  auto validation = manifest.validate();
  if (!validation) return core::Result<Package>{validation.error()};

  for (const auto state : kStates) {
    const auto candidate = canonicalRoot / manifest.assetFor(state);
    std::error_code assetError;
    const auto canonicalAsset = std::filesystem::weakly_canonical(candidate, assetError);
    if (assetError || !std::filesystem::is_regular_file(canonicalAsset, assetError)) {
      return core::failure<Package>(core::ErrorCode::IoError,
                                    "Character state asset is not a regular file",
                                    candidate.string());
    }
    const auto relative = canonicalAsset.lexically_relative(canonicalRoot);
    if (relative.empty() || relative.native().starts_with("..")) {
      return core::failure<Package>(core::ErrorCode::InvariantViolation,
                                    "Character state asset escapes package root",
                                    candidate.string());
    }
  }
  return Package{.root = canonicalRoot, .manifest = std::move(manifest)};
}

}  // namespace seam::character
