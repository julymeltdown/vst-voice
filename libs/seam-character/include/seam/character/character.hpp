#pragma once

#include "seam/character/performance.hpp"
#include "seam/core/result.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace seam::character {

enum class State {
  Neutral,
  Focused,
  Rendering,
  Complete,
  Warning,
  Error,
};

[[nodiscard]] std::string_view stateName(State state) noexcept;
[[nodiscard]] State parseState(std::string_view value) noexcept;

// A schema-one package is status-only: it names its six operating-state assets and nothing else, and a
// presentation that asks it for a mouth gets nothing rather than a guess. Schema two adds declared
// performance assets.
inline constexpr std::int32_t kStatusOnlyManifestSchema{1};
inline constexpr std::int32_t kPerformanceManifestSchema{2};

struct Accent final {
  std::string primary{"#8B4C69"};
  std::string secondary{"#6E5A86"};

  friend bool operator==(const Accent&, const Accent&) = default;
};

struct Manifest final {
  std::int32_t schemaVersion{1};
  std::string characterId;
  std::string displayName;
  std::string version;
  std::string voicebankId;
  std::string style;
  State defaultState{State::Neutral};
  Accent accent;
  std::map<State, std::filesystem::path> stateAssets;
  // Declared only by a performance schema. A package that declares performance must declare every
  // mouth shape it could be asked for: a partially authored turnaround is not a turnaround, and it is
  // refused rather than mixed with the presentation's own fallback drawing.
  std::map<MouthShape, std::filesystem::path> mouthAssets;
  // Whether this artwork is a development turnaround. It travels in the package's own bytes, so moving
  // or renaming the directory cannot promote it to production.
  bool developmentOnly{false};

  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] std::filesystem::path assetFor(State state) const;
  // Empty for a status-only package, or for a shape this package does not declare.
  [[nodiscard]] std::filesystem::path mouthAssetFor(MouthShape shape) const;
  [[nodiscard]] bool declaresPerformance() const noexcept { return !mouthAssets.empty(); }
};

struct Package final {
  std::filesystem::path root;
  Manifest manifest;

  [[nodiscard]] std::filesystem::path assetPath(State state) const;
  [[nodiscard]] std::filesystem::path mouthAssetPath(MouthShape shape) const;
};

[[nodiscard]] core::Result<Package> loadPackage(
    const std::filesystem::path& packageRoot,
    std::uint64_t maximumManifestBytes = 256U * 1024U);

}  // namespace seam::character
