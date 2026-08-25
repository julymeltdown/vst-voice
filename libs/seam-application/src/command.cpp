#include "seam/application/command.hpp"

#include <algorithm>

namespace seam::application {

namespace {

void appendUnique(std::vector<domain::TrackId>& destination,
                  const std::vector<domain::TrackId>& source) {
  for (const auto value : source) {
    if (std::find(destination.begin(), destination.end(), value) ==
        destination.end()) {
      destination.push_back(value);
    }
  }
}

void appendUnique(std::vector<domain::RegionId>& destination,
                  const std::vector<domain::RegionId>& source) {
  for (const auto value : source) {
    if (std::find(destination.begin(), destination.end(), value) ==
        destination.end()) {
      destination.push_back(value);
    }
  }
}

void appendUnique(std::vector<domain::NoteId>& destination,
                  const std::vector<domain::NoteId>& source) {
  for (const auto value : source) {
    if (std::find(destination.begin(), destination.end(), value) ==
        destination.end()) {
      destination.push_back(value);
    }
  }
}

void appendUnique(std::vector<domain::LyricTokenId>& destination,
                  const std::vector<domain::LyricTokenId>& source) {
  for (const auto value : source) {
    if (std::find(destination.begin(), destination.end(), value) ==
        destination.end()) {
      destination.push_back(value);
    }
  }
}

void mergeImpact(CommandImpact& destination, const CommandImpact& source) {
  if (static_cast<int>(source.scope) > static_cast<int>(destination.scope)) {
    destination.scope = source.scope;
  }
  destination.projectWide = destination.projectWide || source.projectWide;
  appendUnique(destination.trackIds, source.trackIds);
  appendUnique(destination.regionIds, source.regionIds);
  appendUnique(destination.noteIds, source.noteIds);
  appendUnique(destination.lyricIds, source.lyricIds);
}

}

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

CommandImpact CompositeCommand::impact() const {
  CommandImpact result;
  for (const auto& command : commands_) {
    if (command != nullptr) mergeImpact(result, command->impact());
  }
  return result;
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
