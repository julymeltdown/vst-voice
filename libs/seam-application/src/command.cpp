#include "seam/application/command.hpp"

namespace seam::application {

CompositeCommand::CompositeCommand(std::string name) : name_(std::move(name)) {}

void CompositeCommand::add(std::unique_ptr<ICommand> command) {
  if (command) commands_.push_back(std::move(command));
}

CommandAudioImpact CompositeCommand::audioImpact() const noexcept {
  auto impact = CommandAudioImpact::ViewOnly;
  for (const auto& command : commands_) {
    if (command == nullptr) continue;
    const auto current = command->audioImpact();
    if (static_cast<int>(current) > static_cast<int>(impact)) impact = current;
  }
  return impact;
}

core::Result<void> CompositeCommand::apply(domain::Project& project) {
  const auto transactionSnapshot = project;
  for (auto& command : commands_) {
    const auto result = command->apply(project);
    if (!result) {
      project = transactionSnapshot;
      return result;
    }
  }
  return core::success();
}

core::Result<void> CompositeCommand::revert(domain::Project& project) {
  const auto transactionSnapshot = project;
  for (auto iterator = commands_.rbegin(); iterator != commands_.rend(); ++iterator) {
    const auto result = (*iterator)->revert(project);
    if (!result) {
      project = transactionSnapshot;
      return result;
    }
  }
  return core::success();
}

}  // namespace seam::application
