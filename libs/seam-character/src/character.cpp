#include "seam/character/character.hpp"

#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"

#include <array>
#include <cctype>
#include <cmath>
#include <system_error>

namespace seam::character {
namespace {

constexpr std::array<State, 6> kStates{
    State::Neutral, State::Focused, State::Rendering,
    State::Complete, State::Warning, State::Error};

constexpr std::array<MouthShape, 6> kMouthShapes{
    MouthShape::Closed, MouthShape::Narrow, MouthShape::Nasal,
    MouthShape::Open, MouthShape::Wide, MouthShape::Round};

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

std::optional<State> stateNamed(std::string_view name) noexcept {
  for (const auto state : kStates)
    if (stateName(state) == name) return state;
  return std::nullopt;
}

// A normalized rectangle (mouth placement or eye box): finite, non-empty and inside the unit square.
bool insideUnitSquare(const MouthPlacement& r) noexcept {
  return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) &&
         std::isfinite(r.height) && r.x >= 0.0 && r.y >= 0.0 && r.width > 0.0 &&
         r.height > 0.0 && r.width <= 1.0 && r.height <= 1.0 && r.x + r.width <= 1.0 &&
         r.y + r.height <= 1.0;
}

core::Result<MouthPlacement> parseRect(const formats::JsonValue& value, std::string_view what) {
  if (!value.isObject())
    return core::failure<MouthPlacement>(core::ErrorCode::ParseError,
                                         "Character rectangle must be an object", std::string{what});
  const auto* x = value.find("x");
  const auto* y = value.find("y");
  const auto* width = value.find("width");
  const auto* height = value.find("height");
  if (x == nullptr || y == nullptr || width == nullptr || height == nullptr || !x->isNumber() ||
      !y->isNumber() || !width->isNumber() || !height->isNumber())
    return core::failure<MouthPlacement>(
        core::ErrorCode::ParseError, "Character rectangle needs numeric x, y, width and height",
        std::string{what});
  return MouthPlacement{x->asNumber(), y->asNumber(), width->asNumber(), height->asNumber()};
}

// "eyes": { "<state>": [ {x, y, width, height}, ... ] } with one or two boxes per named state.
core::Result<std::map<State, std::vector<EyeBox>>> parseEyes(const formats::JsonValue& value) {
  using Eyes = std::map<State, std::vector<EyeBox>>;
  if (!value.isObject())
    return core::failure<Eyes>(core::ErrorCode::ParseError, "Character eyes must be an object");
  Eyes eyes;
  for (const auto& [name, boxes] : value.asObject()) {
    const auto state = stateNamed(name);
    if (!state.has_value())
      return core::failure<Eyes>(core::ErrorCode::ParseError,
                                 "Character eyes name an unknown state", name);
    if (!boxes.isArray() || boxes.asArray().empty() ||
        boxes.asArray().size() > kMaximumEyesPerState)
      return core::failure<Eyes>(core::ErrorCode::ParseError,
                                 "Character eyes list one or two boxes per state", name);
    auto& list = eyes[*state];
    for (const auto& box : boxes.asArray()) {
      auto rect = parseRect(box, name);
      if (!rect) return core::Result<Eyes>{rect.error()};
      list.push_back(rect.value());
    }
  }
  return eyes;
}

core::Result<std::map<State, std::filesystem::path>> parseStates(const formats::JsonValue* states) {
  using States = std::map<State, std::filesystem::path>;
  if (states == nullptr || !states->isObject())
    return core::failure<States>(core::ErrorCode::ParseError,
                                 "Character manifest states object is missing");
  States result;
  for (const auto state : kStates) {
    const auto key = stateName(state);
    const auto* value = states->find(key);
    if (value == nullptr || !value->isString())
      return core::failure<States>(core::ErrorCode::ParseError, "Character state asset is missing",
                                   std::string{key});
    result.emplace(state, std::filesystem::path{value->asString()});
  }
  return result;
}

core::Result<std::map<MouthShape, std::filesystem::path>> parseMouths(
    const formats::JsonValue& mouths) {
  using Mouths = std::map<MouthShape, std::filesystem::path>;
  if (!mouths.isObject())
    return core::failure<Mouths>(core::ErrorCode::ParseError,
                                 "Character manifest mouths must be an object");
  Mouths result;
  for (const auto shape : kMouthShapes) {
    const auto key = mouthShapeName(shape);
    const auto* value = mouths.find(key);
    if (value == nullptr) continue;
    if (!value->isString() || value->asString().empty())
      return core::failure<Mouths>(core::ErrorCode::ParseError,
                                   "Character performance asset path must be a string",
                                   std::string{key});
    result.emplace(shape, std::filesystem::path{value->asString()});
  }
  return result;
}

bool validOutfitName(std::string_view name) noexcept {
  if (name.empty() || name.size() > 32U) return false;
  for (const auto c : name)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
  return true;
}

core::Result<void> validateEyes(const std::map<State, std::vector<EyeBox>>& eyes) {
  for (const auto& [state, boxes] : eyes) {
    if (boxes.empty() || boxes.size() > kMaximumEyesPerState)
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Character eyes list one or two boxes per state",
                           std::string{stateName(state)});
    for (const auto& box : boxes)
      if (!insideUnitSquare(box))
        return core::failure(core::ErrorCode::InvariantViolation,
                             "Character eye box is outside the portrait bounds",
                             std::string{stateName(state)});
  }
  return core::success();
}

// A declared asset must be a regular file that stays inside the package root.
core::Result<void> checkAsset(const std::filesystem::path& canonicalRoot,
                              const std::filesystem::path& relativeAsset, const char* what) {
  const auto candidate = canonicalRoot / relativeAsset;
  std::error_code assetError;
  const auto canonicalAsset = std::filesystem::weakly_canonical(candidate, assetError);
  if (assetError || !std::filesystem::is_regular_file(canonicalAsset, assetError))
    return core::failure(core::ErrorCode::IoError, what, candidate.string());
  const auto relative = canonicalAsset.lexically_relative(canonicalRoot);
  if (relative.empty() ||
      (relative.begin() != relative.end() && *relative.begin() == std::filesystem::path{".."}))
    return core::failure(core::ErrorCode::InvariantViolation, "Character asset escapes package root",
                         candidate.string());
  return core::success();
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
  if (schemaVersion != kStatusOnlyManifestSchema &&
      schemaVersion != kPerformanceManifestSchema &&
      schemaVersion != kResourceBoundManifestSchema) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Unsupported character manifest schema",
                         std::to_string(schemaVersion));
  }
  if (schemaVersion == kStatusOnlyManifestSchema) {
    // A status-only package cannot claim a performance field of any kind: that would let a package
    // look like a turnaround while its schema says a reader may not expect one.
    if (declaresPerformance() || developmentOnly || mouthPlacement.has_value() ||
        resourceIdentity.has_value() || !eyes.empty() || !outfits.empty()) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "A status-only character package declares no performance assets");
    }
  }
  if (characterId.empty() || displayName.empty() || version.empty() ||
      voicebankId.empty() || style.empty()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Character manifest identity fields must not be empty");
  }
  if (schemaVersion == kResourceBoundManifestSchema) {
    if (!resourceIdentity.has_value())
      return core::failure(core::ErrorCode::InvariantViolation,
                           "A schema-three character package requires an exact singer resource identity");
    const auto identity = resourceIdentity->validate();
    if (!identity) return identity;
    if (voicebankId != resourceIdentity->id)
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Character legacy voicebank ID must match its bound singer resource ID");
  } else if (resourceIdentity.has_value()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "An exact singer resource identity requires character schema three");
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
  if (schemaVersion == kPerformanceManifestSchema ||
      schemaVersion == kResourceBoundManifestSchema) {
    for (const auto shape : kMouthShapes) {
      const auto iterator = mouthAssets.find(shape);
      if (iterator == mouthAssets.end() || !safeRelativeAsset(iterator->second)) {
        return core::failure(core::ErrorCode::InvariantViolation,
                             "Character performance asset is missing or unsafe",
                             std::string{mouthShapeName(shape)});
      }
    }
    if (mouthPlacement.has_value()) {
      if (!insideUnitSquare(*mouthPlacement)) {
        return core::failure(core::ErrorCode::InvariantViolation,
                             "Character mouth placement is outside the portrait bounds");
      }
    }
    if (auto valid = validateEyes(eyes); !valid) return valid;
    for (const auto& [name, outfit] : outfits) {
      if (!validOutfitName(name))
        return core::failure(core::ErrorCode::InvariantViolation,
                             "Character outfit names are lowercase letters, digits and hyphens",
                             name);
      for (const auto state : kStates) {
        const auto iterator = outfit.stateAssets.find(state);
        if (iterator == outfit.stateAssets.end() || !safeRelativeAsset(iterator->second))
          return core::failure(core::ErrorCode::InvariantViolation,
                               "Character outfit state asset is missing or unsafe",
                               name + "/" + std::string{stateName(state)});
      }
      // An outfit's mouths are all six or none, like the shared set's.
      if (!outfit.mouthAssets.empty()) {
        for (const auto shape : kMouthShapes) {
          const auto iterator = outfit.mouthAssets.find(shape);
          if (iterator == outfit.mouthAssets.end() || !safeRelativeAsset(iterator->second))
            return core::failure(core::ErrorCode::InvariantViolation,
                                 "Character outfit mouth asset is missing or unsafe",
                                 name + "/" + std::string{mouthShapeName(shape)});
        }
      } else if (outfit.mouthPlacement.has_value()) {
        return core::failure(core::ErrorCode::InvariantViolation,
                             "A character outfit places mouths it does not declare", name);
      }
      if (outfit.mouthPlacement.has_value() && !insideUnitSquare(*outfit.mouthPlacement))
        return core::failure(core::ErrorCode::InvariantViolation,
                             "Character outfit mouth placement is outside the portrait bounds",
                             name);
      if (auto valid = validateEyes(outfit.eyes); !valid) return valid;
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

std::filesystem::path Manifest::mouthAssetFor(MouthShape shape) const {
  const auto iterator = mouthAssets.find(shape);
  return iterator == mouthAssets.end() ? std::filesystem::path{} : iterator->second;
}

std::filesystem::path Package::mouthAssetPath(MouthShape shape) const {
  const auto relative = manifest.mouthAssetFor(shape);
  return relative.empty() ? std::filesystem::path{} : root / relative;
}

const Outfit* Manifest::outfit(std::string_view name) const {
  if (name.empty()) return nullptr;
  const auto iterator = outfits.find(std::string{name});
  return iterator == outfits.end() ? nullptr : &iterator->second;
}

std::filesystem::path Manifest::assetFor(State state, std::string_view outfitName) const {
  if (const auto* own = outfit(outfitName); own != nullptr) {
    const auto iterator = own->stateAssets.find(state);
    if (iterator != own->stateAssets.end()) return iterator->second;
  }
  return assetFor(state);
}

std::filesystem::path Manifest::mouthAssetFor(MouthShape shape, std::string_view outfitName) const {
  // An outfit's singing face is its own picture: it uses its own sprites or none.
  if (const auto* own = outfit(outfitName); own != nullptr) {
    const auto iterator = own->mouthAssets.find(shape);
    return iterator == own->mouthAssets.end() ? std::filesystem::path{} : iterator->second;
  }
  return mouthAssetFor(shape);
}

std::optional<MouthPlacement> Manifest::mouthPlacementFor(std::string_view outfitName) const {
  if (const auto* own = outfit(outfitName); own != nullptr) return own->mouthPlacement;
  return mouthPlacement;
}

std::vector<EyeBox> Manifest::eyesFor(State state, std::string_view outfitName) const {
  if (const auto* own = outfit(outfitName); own != nullptr) {
    const auto iterator = own->eyes.find(state);
    if (iterator != own->eyes.end()) return iterator->second;
  }
  const auto iterator = eyes.find(state);
  return iterator == eyes.end() ? std::vector<EyeBox>{} : iterator->second;
}

std::filesystem::path Package::assetPath(State state, std::string_view outfitName) const {
  return root / manifest.assetFor(state, outfitName);
}

std::filesystem::path Package::mouthAssetPath(MouthShape shape, std::string_view outfitName) const {
  const auto relative = manifest.mouthAssetFor(shape, outfitName);
  return relative.empty() ? std::filesystem::path{} : root / relative;
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
  if (const auto* singer = parsed.value().find("singerResource"); singer != nullptr) {
    if (!singer->isObject())
      return core::failure<Package>(core::ErrorCode::ParseError,
                                    "Character singerResource must be an object");
    const auto* kindValue = singer->find("kind");
    if (kindValue == nullptr || !kindValue->isString())
      return core::failure<Package>(core::ErrorCode::ParseError,
                                    "Character singerResource kind is missing");
    domain::SingerResourceKind kind;
    if (kindValue->asString() == "sample") kind = domain::SingerResourceKind::Sample;
    else if (kindValue->asString() == "procedural") kind = domain::SingerResourceKind::Procedural;
    else if (kindValue->asString() == "neural") kind = domain::SingerResourceKind::Neural;
    else return core::failure<Package>(core::ErrorCode::ParseError,
                                       "Character singerResource kind is unknown");
    auto singerId = requiredString(*singer, "id");
    auto singerVersion = requiredString(*singer, "version");
    auto singerHash = requiredString(*singer, "contentHash");
    if (!singerId) return core::Result<Package>{singerId.error()};
    if (!singerVersion) return core::Result<Package>{singerVersion.error()};
    if (!singerHash) return core::Result<Package>{singerHash.error()};
    manifest.resourceIdentity = domain::SingerResourceIdentity{
        .kind = kind,
        .id = std::move(singerId.value()),
        .version = std::move(singerVersion.value()),
        .contentHash = std::move(singerHash.value())};
  }
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
  auto states = parseStates(parsed.value().find("states"));
  if (!states) return core::Result<Package>{states.error()};
  manifest.stateAssets = std::move(states.value());
  // Performance fields are read whenever they are present and then validated against the schema, so a
  // status-only package that carries them is refused instead of silently ignoring them.
  if (const auto* mouths = parsed.value().find("mouths"); mouths != nullptr) {
    auto parsedMouths = parseMouths(*mouths);
    if (!parsedMouths) return core::Result<Package>{parsedMouths.error()};
    manifest.mouthAssets = std::move(parsedMouths.value());
  }
  if (const auto* placement = parsed.value().find("mouthPlacement"); placement != nullptr) {
    auto rect = parseRect(*placement, "mouthPlacement");
    if (!rect) return core::Result<Package>{rect.error()};
    manifest.mouthPlacement = rect.value();
  }
  if (const auto* eyes = parsed.value().find("eyes"); eyes != nullptr) {
    auto parsedEyes = parseEyes(*eyes);
    if (!parsedEyes) return core::Result<Package>{parsedEyes.error()};
    manifest.eyes = std::move(parsedEyes.value());
  }
  if (const auto* outfits = parsed.value().find("outfits"); outfits != nullptr) {
    if (!outfits->isObject())
      return core::failure<Package>(core::ErrorCode::ParseError,
                                    "Character outfits must be an object");
    for (const auto& [outfitName, value] : outfits->asObject()) {
      if (!value.isObject())
        return core::failure<Package>(core::ErrorCode::ParseError,
                                      "Character outfit must be an object", outfitName);
      Outfit outfit;
      auto outfitStates = parseStates(value.find("states"));
      if (!outfitStates) return core::Result<Package>{outfitStates.error()};
      outfit.stateAssets = std::move(outfitStates.value());
      if (const auto* mouths = value.find("mouths"); mouths != nullptr) {
        auto parsedMouths = parseMouths(*mouths);
        if (!parsedMouths) return core::Result<Package>{parsedMouths.error()};
        outfit.mouthAssets = std::move(parsedMouths.value());
      }
      if (const auto* placement = value.find("mouthPlacement"); placement != nullptr) {
        auto rect = parseRect(*placement, outfitName);
        if (!rect) return core::Result<Package>{rect.error()};
        outfit.mouthPlacement = rect.value();
      }
      if (const auto* eyes = value.find("eyes"); eyes != nullptr) {
        auto parsedEyes = parseEyes(*eyes);
        if (!parsedEyes) return core::Result<Package>{parsedEyes.error()};
        outfit.eyes = std::move(parsedEyes.value());
      }
      manifest.outfits.emplace(outfitName, std::move(outfit));
    }
  }
  if (const auto* developmentOnly = parsed.value().find("developmentOnly");
      developmentOnly != nullptr) {
    if (!developmentOnly->isBool()) {
      return core::failure<Package>(core::ErrorCode::ParseError,
                                    "Character manifest developmentOnly must be a boolean");
    }
    manifest.developmentOnly = developmentOnly->asBool();
  }
  auto validation = manifest.validate();
  if (!validation) return core::Result<Package>{validation.error()};

  for (const auto state : kStates) {
    auto checked = checkAsset(canonicalRoot, manifest.assetFor(state),
                              "Character state asset is not a regular file");
    if (!checked) return core::Result<Package>{checked.error()};
  }
  for (const auto shape : kMouthShapes) {
    const auto relativeAsset = manifest.mouthAssetFor(shape);
    if (relativeAsset.empty()) continue;
    auto checked = checkAsset(canonicalRoot, relativeAsset,
                              "Character performance asset is not a regular file");
    if (!checked) return core::Result<Package>{checked.error()};
  }
  for (const auto& [outfitName, outfit] : manifest.outfits) {
    for (const auto& [state, relativeAsset] : outfit.stateAssets) {
      auto checked = checkAsset(canonicalRoot, relativeAsset,
                                "Character outfit state asset is not a regular file");
      if (!checked) return core::Result<Package>{checked.error()};
    }
    for (const auto& [shape, relativeAsset] : outfit.mouthAssets) {
      auto checked = checkAsset(canonicalRoot, relativeAsset,
                                "Character outfit mouth asset is not a regular file");
      if (!checked) return core::Result<Package>{checked.error()};
    }
  }
  return Package{.root = canonicalRoot, .manifest = std::move(manifest)};
}

}  // namespace seam::character
