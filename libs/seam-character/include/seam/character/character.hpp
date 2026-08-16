#pragma once

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

  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] std::filesystem::path assetFor(State state) const;
};

struct Package final {
  std::filesystem::path root;
  Manifest manifest;

  [[nodiscard]] std::filesystem::path assetPath(State state) const;
};

[[nodiscard]] core::Result<Package> loadPackage(
    const std::filesystem::path& packageRoot,
    std::uint64_t maximumManifestBytes = 256U * 1024U);

}  // namespace seam::character
