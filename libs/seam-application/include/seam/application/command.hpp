#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace seam::application {

enum class CommandAudioImpact {
  ViewOnly,
  MetadataOnly,
  PhraseAudio,
  TrackMix,
  ProjectAudio,
};

class ICommand {
public:
  virtual ~ICommand() = default;
  [[nodiscard]] virtual std::string_view name() const noexcept = 0;
  [[nodiscard]] virtual CommandAudioImpact audioImpact() const noexcept {
    return CommandAudioImpact::ProjectAudio;
  }
  [[nodiscard]] virtual core::Result<void> apply(domain::Project& project) = 0;
  [[nodiscard]] virtual core::Result<void> revert(domain::Project& project) = 0;
};

class CompositeCommand final : public ICommand {
public:
  explicit CompositeCommand(std::string name);
  void add(std::unique_ptr<ICommand> command);

  [[nodiscard]] std::string_view name() const noexcept override { return name_; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  std::string name_;
  std::vector<std::unique_ptr<ICommand>> commands_;
};

}  // namespace seam::application
