#include "seam/application/command.hpp"

namespace seam::application {

CompositeCommand::CompositeCommand(std::string name) : name_(std::move(name)) {}

void CompositeCommand::add(std::unique_ptr<ICommand> command) {
  if (command) {
    commands_.push_back(std::move(command));
  }
}

core::Result<void> CompositeCommand::apply(domain::Project& project) {
  std::size_t applied = 0;
  for (auto& command : commands_) {
    const auto result = command->apply(project);
    if (!result) {
      while (applied > 0) {
        --applied;
        static_cast<void>(commands_[applied]->revert(project));
      }
      return result;
    }
    ++applied;
  }
  return core::success();
}

core::Result<void> CompositeCommand::revert(domain::Project& project) {
  for (auto iterator = commands_.rbegin(); iterator != commands_.rend(); ++iterator) {
    const auto result = (*iterator)->revert(project);
    if (!result) {
      return result;
    }
  }
  return core::success();
}

}  // namespace seam::application
